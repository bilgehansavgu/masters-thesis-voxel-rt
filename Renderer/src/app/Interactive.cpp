#include "Interactive.h"

#include "ControlPanel.h"
#include "FramePipeline.h"
#include "Gui.h"
#include "Presenter.h"
#include "RayWorkSummary.h"
#include "SceneSetup.h"
#include "Window.h"

#include "thesis/gpu/Buffer.h"
#include "thesis/gpu/Context.h"
#include "thesis/metrics/Sinks.h"
#include "thesis/profiling/Profiling.h"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>

namespace thesis::app {
namespace {

// Everything that exists once per built scene. Rebuilt when the user picks another encoding or
// another brick size.
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

// --self-test: uses every control of the panel once, as a user can with the keys. It runs
// "Measure all", shows each view, then goes through the encodings, the brick sizes and the tracers.
class PanelTour {
public:
    explicit PanelTour(const PanelFacts& facts)
    {
        steps_.push_back([](ControlPanel& panel) { panel.measureAll(); });
        for (std::size_t mode = 0; mode < resolve::kShadingModeNames.size(); ++mode) {
            steps_.push_back(
                [mode](ControlPanel& panel) { panel.selectView(static_cast<resolve::ShadingMode>(mode)); });
        }
        steps_.push_back([](ControlPanel& panel) { panel.selectView(resolve::ShadingMode::Shaded); });
        // The ray inspector on the ray in the middle of the frame, then off again.
        steps_.push_back([width = facts.width, height = facts.height](ControlPanel& panel) {
            panel.inspectPixel(width / 2, height / 2);
        });
        steps_.push_back([](ControlPanel& panel) { panel.stopInspecting(); });
        steps_.insert(steps_.end(), facts.encodings.size(), [](ControlPanel& panel) { panel.selectNextEncoding(); });
        steps_.insert(steps_.end(), facts.brickSizes.size(), [](ControlPanel& panel) { panel.selectNextBrickSize(); });
        steps_.insert(steps_.end(), facts.tracers.size(), [](ControlPanel& panel) { panel.selectNextTracer(); });
    }

    // Call once for each frame. Returns false when the tour is over.
    bool advance(ControlPanel& panel)
    {
        if (panel.measuring() || ++framesSinceStep_ < kFramesPerStep) {
            return true;
        }
        if (next_ == steps_.size()) {
            return false;
        }
        framesSinceStep_ = 0;
        steps_[next_++](panel);
        return true;
    }

private:
    static constexpr std::uint32_t kFramesPerStep = 8;
    std::vector<std::function<void(ControlPanel&)>> steps_;
    std::size_t next_ = 0;
    std::uint32_t framesSinceStep_ = 0;
};

} // namespace

int runInteractive(const Options& options)
{
    Window window(1600, 900, "Thesis Renderer");

    gpu::ContextOptions contextOptions;
    contextOptions.validation = options.validation;
    contextOptions.instanceExtensions = Window::requiredInstanceExtensions();
    contextOptions.createSurface = [&](VkInstance instance) { return window.createSurface(instance); };
    const gpu::Context context(contextOptions);

    SceneSource source(options);
    const tracing::TracerRegistry registry = makeTracerRegistry();
    const tracing::TracerEnvironment environment{context, options.shaderDirectory};
    FramePipeline pipeline(context, options.shaderDirectory, {options.width, options.height});

    // Builds the scene for one encoding and one brick size, uploads it and prepares every
    // tracer for it, so that switching tracers is instant. Fills `facts` with what the panel shows.
    const auto load = [&](std::size_t encoding, std::uint32_t brickSize, PanelFacts& facts) {
        const BuiltScene built = source.build(encoding, brickSize);
        auto active = std::make_unique<ActiveScene>();
        active->gpuScene = tracing::GpuScene::upload(context, built.scene);

        facts = {};
        for (const SceneSource::EncodingChoice& choice : source.encodings()) {
            facts.encodings.push_back({choice.name, choice.description});
        }
        facts.encoding = encoding;
        facts.brickSizes = source.brickSizes();
        facts.brickSize = built.brickSize;
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

        for (const auto& entry : registry.entries()) {
            auto tracer = registry.create(entry.name, environment);
            try {
                const tracing::PrepareReport report = tracer->prepare(active->gpuScene);
                facts.tracers.push_back({entry.name, entry.description, tracer->memoryBytes(),
                                         report.gpuMilliseconds + report.cpuMilliseconds, report.cpuMilliseconds > 0.0,
                                         tracer->countsWork(), tracer->findsHits()});
                active->tracers.push_back(std::move(tracer));
            } catch (const tracing::UnsupportedScene& reason) {
                facts.skippedTracers.push_back(entry.name + ": " + reason.what());
            }
        }
        if (active->tracers.empty()) {
            throw std::runtime_error("No tracer can handle scene '" + built.scene.name + "'");
        }
        return active;
    };

    PanelFacts facts;
    std::size_t loadedEncoding = source.initialEncoding();
    std::uint32_t loadedBrickSize = source.initialBrickSize();
    std::unique_ptr<ActiveScene> active = load(loadedEncoding, loadedBrickSize, facts);
    for (const std::string& skipped : facts.skippedTracers) {
        std::printf("note: tracer not available for this scene: %s\n", skipped.c_str());
    }

    Presenter presenter(context, window);
    Gui gui(context, window, presenter.format(), presenter.imageCount());
    const std::size_t initialTracer = findTracer(facts.tracers, options.tracers, "hardware");
    std::optional<PanelTour> tour;
    if (options.selfTest) {
        tour.emplace(facts);
    }
    ControlPanel panel(std::move(facts), startCamera(options, active->gpuScene.bounds), initialTracer);
    if (options.inspect.size() == 2) {
        panel.inspectPixel(options.inspect[0], options.inspect[1]);
    }
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

            // What the counting variant wrote in that frame: the steps of the inspected ray, and
            // (about once a second, while a work view is on) the average work of the rays.
            panel.recordRayLog(pipeline.rayLog());
            if (panel.wantsWorkSummary() && pipeline.work() != nullptr) {
                const std::size_t count = pipeline.batch().count();
                const std::vector<std::byte> workBytes = gpu::downloadBuffer(context, *pipeline.work());
                const std::vector<std::byte> hitBytes = gpu::downloadBuffer(context, pipeline.batch().hits);
                RayWorkSummary summary;
                summary.add({reinterpret_cast<const core::RayWork*>(workBytes.data()), count},
                            {reinterpret_cast<const core::Hit*>(hitBytes.data()), count});
                panel.recordWorkSummary(summary);
            }
        }

        // The user picked another encoding or another brick size. Nothing runs on the GPU right
        // now, so the old scene can be freed before the new one is built (both would not always
        // fit in GPU memory).
        if (panel.wantedEncoding() != loadedEncoding || panel.wantedBrickSize() != loadedBrickSize) {
            active.reset();
            PanelFacts newFacts;
            try {
                active = load(panel.wantedEncoding(), panel.wantedBrickSize(), newFacts);
                loadedEncoding = panel.wantedEncoding();
                loadedBrickSize = panel.wantedBrickSize();
            } catch (const std::exception& error) {
                // The model cannot be stored that way. Show why, and go back to the scene before.
                std::fprintf(stderr, "error: %s\n", error.what());
                panel.reportProblem(error.what());
                active = load(loadedEncoding, loadedBrickSize, newFacts);
            }
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
        pipeline.setLoggedRay(panel.loggedRay());
        pipeline.trace(*cmd, *active->tracers[panel.activeTracer()], panel.camera().frame(pipeline.aspectRatio()),
                       panel.countWork());
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
        if (tour && !tour->advance(panel)) {
            window.requestClose();
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
