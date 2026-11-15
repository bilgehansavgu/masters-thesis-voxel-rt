#include "Interactive.h"

#include "ControlPanel.h"
#include "FramePipeline.h"
#include "Gui.h"
#include "Presenter.h"
#include "SceneSetup.h"
#include "Window.h"

#include "thesis/gpu/Context.h"
#include "thesis/metrics/Sinks.h"
#include "thesis/profiling/Profiling.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <utility>

namespace thesis::app {
namespace {

// Everything that exists once per built scene. Rebuilt when the user picks another encoding.
struct ActiveScene {
    tracing::GpuScene gpuScene; // declared before the tracers: they point at it and are destroyed first
    std::vector<std::unique_ptr<tracing::ITracer>> tracers;
};

// The tracer to show first: the one named on the command line, else `preferred` if it is
// available, else the first one.
std::size_t findTracer(const std::vector<PanelFacts::Tracer>& tracers, const std::vector<std::string>& requested,
                       const std::string& preferred)
{
    const auto indexOf = [&](const std::string& name) {
        const auto it = std::find_if(tracers.begin(), tracers.end(),
                                     [&](const PanelFacts::Tracer& tracer) { return tracer.name == name; });
        return static_cast<std::size_t>(it - tracers.begin());
    };
    if (!requested.empty()) {
        const std::size_t index = indexOf(requested.front());
        if (index == tracers.size()) {
            throw std::invalid_argument("Tracer '" + requested.front() + "' is unknown or cannot handle this scene");
        }
        return index;
    }
    const std::size_t index = indexOf(preferred);
    return index == tracers.size() ? 0 : index;
}

} // namespace

int runInteractive(const Options& options)
{
    Window window(1600, 900, "Thesis Renderer");

    gpu::ContextOptions contextOptions;
    contextOptions.validation = options.validation;
    contextOptions.instanceExtensions = Window::requiredInstanceExtensions();
    contextOptions.createSurface = [&](VkInstance instance) { return window.createSurface(instance); };
    const gpu::Context context(contextOptions);

    const SceneSource source(options);
    const tracing::TracerRegistry registry = makeTracerRegistry();
    const tracing::TracerEnvironment environment{context, options.shaderDirectory};
    FramePipeline pipeline(context, options.shaderDirectory, {options.width, options.height});

    // Builds the scene for one encoding, uploads it and prepares every tracer for it, so that
    // switching tracers is instant. Fills `facts` with what the panel shows.
    const auto load = [&](std::size_t encoding, PanelFacts& facts) {
        const BuiltScene built = source.build(encoding);
        auto active = std::make_unique<ActiveScene>();
        active->gpuScene = tracing::GpuScene::upload(context, built.scene);

        facts = {};
        facts.encodings = source.encodings();
        facts.sceneName = built.scene.name;
        facts.sceneDetails = built.details;
        facts.primitiveCount = active->gpuScene.primitiveCount;
        facts.voxelCount = built.voxels;
        for (const encoding::MemoryReport::Buffer& buffer : built.memory.buffers) {
            facts.sceneBuffers.push_back({buffer.name, buffer.bytes});
        }
        facts.sceneBytes = active->gpuScene.memoryBytes();
        facts.encodeMilliseconds = 1e3 * built.encodeSeconds;
        facts.width = options.width;
        facts.height = options.height;
        facts.rayAndHitBytes = pipeline.batch().memoryBytes();

        for (const std::string& name : registry.names()) {
            auto tracer = registry.create(name, environment);
            try {
                const tracing::PrepareReport report = tracer->prepare(active->gpuScene);
                facts.tracers.push_back({name, tracer->memoryBytes(), report.gpuMilliseconds});
                active->tracers.push_back(std::move(tracer));
            } catch (const tracing::UnsupportedScene& reason) {
                facts.skippedTracers.push_back(name + ": " + reason.what());
            }
        }
        if (active->tracers.empty()) {
            throw std::runtime_error("No tracer can handle scene '" + built.scene.name + "'");
        }
        return active;
    };

    PanelFacts facts;
    std::size_t loadedEncoding = source.initialEncoding();
    std::unique_ptr<ActiveScene> active = load(loadedEncoding, facts);
    for (const std::string& skipped : facts.skippedTracers) {
        std::printf("note: tracer not available for this scene: %s\n", skipped.c_str());
    }

    Presenter presenter(context, window);
    Gui gui(context, window, presenter.format(), presenter.imageCount());
    // A voxel model has far too many bricks for the software tracer of step 1 to be pleasant,
    // so the window starts with the hardware tracer there.
    const std::size_t initialTracer =
        findTracer(facts.tracers, options.tracers, source.isVoxelModel() ? "hardware" : "software");
    ControlPanel panel(std::move(facts), core::OrbitCamera::framing(active->gpuScene.bounds), initialTracer,
                       loadedEncoding);
    metrics::TracySink tracySink;
    std::uint64_t frameIndex = 0;

    while (!window.shouldClose()) {
        window.pollEvents();

        // The previous frame has finished on the GPU, so its timings can be read.
        if (presenter.waitForPreviousFrame()) {
            const std::vector<gpu::StageTiming> timings = pipeline.collectTimings();
            panel.recordTimings(timings);
            metrics::FrameSample sample{0, frameIndex, pipeline.batch().count(), {}};
            for (const gpu::StageTiming& timing : timings) {
                sample.stages.push_back({timing.stage, timing.milliseconds});
            }
            tracySink.recordFrame(sample);
        }

        // The user picked another encoding. Nothing runs on the GPU right now, so the old scene
        // can be freed before the new one is built (both would not always fit in GPU memory).
        if (panel.activeEncoding() != loadedEncoding) {
            active.reset();
            PanelFacts newFacts;
            active = load(panel.activeEncoding(), newFacts);
            loadedEncoding = panel.activeEncoding();
            panel.setFacts(std::move(newFacts));
        }

        const std::optional<VkCommandBuffer> cmd = presenter.beginFrame();
        if (!cmd) {
            gui.swapchainRebuilt();
            continue;
        }

        gui.newFrame();
        panel.handleInput();
        panel.draw();

        pipeline.begin(*cmd);
        pipeline.trace(*cmd, *active->tracers[panel.activeTracer()], panel.camera().frame(pipeline.aspectRatio()));
        pipeline.shade(*cmd, panel.shading());
        const bool rebuilt =
            presenter.present(pipeline.output(), [&](VkCommandBuffer overlayCmd, VkImageView view, VkExtent2D extent) {
                gui.render(overlayCmd, view, extent);
            });
        if (rebuilt) {
            gui.swapchainRebuilt();
        }
        THESIS_FRAME_MARK();

        ++frameIndex;
        if (options.switchEncodingEvery > 0 && frameIndex % options.switchEncodingEvery == 0) {
            panel.selectNextEncoding();
        }
        if (options.exitAfterFrames > 0 && frameIndex >= options.exitAfterFrames) {
            window.requestClose();
        }
    }

    presenter.waitForPreviousFrame();
    if (options.validation && gpu::validationMessageCount() > 0) {
        std::fprintf(stderr, "%llu validation message(s) were reported.\n",
                     static_cast<unsigned long long>(gpu::validationMessageCount()));
        return 2;
    }
    return 0;
}

} // namespace thesis::app
