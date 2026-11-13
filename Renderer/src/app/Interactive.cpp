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

namespace thesis::app {
namespace {

std::size_t findTracer(const std::vector<std::string>& names, const std::vector<std::string>& requested)
{
    if (requested.empty()) {
        return 0;
    }
    const auto it = std::find(names.begin(), names.end(), requested.front());
    if (it == names.end()) {
        throw std::invalid_argument("Unknown tracer '" + requested.front() + "'");
    }
    return static_cast<std::size_t>(it - names.begin());
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

    // Scene and tracers. Every tracer is built up front, so switching is instant.
    const core::Scene scene = makeScene(options);
    const tracing::GpuScene gpuScene = tracing::GpuScene::upload(context, scene);
    const tracing::TracerRegistry registry = makeTracerRegistry();
    const tracing::TracerEnvironment environment{context, options.shaderDirectory};
    const std::vector<std::string> tracerNames = registry.names();

    FramePipeline pipeline(context, options.shaderDirectory, {options.width, options.height});
    PanelFacts facts{{},
                     scene.name,
                     gpuScene.primitiveCount,
                     gpuScene.memoryBytes(),
                     options.width,
                     options.height,
                     pipeline.batch().memoryBytes()};
    std::vector<std::unique_ptr<tracing::ITracer>> tracers;
    for (const std::string& name : tracerNames) {
        tracers.push_back(registry.create(name, environment));
        const tracing::PrepareReport report = tracers.back()->prepare(gpuScene);
        facts.tracers.push_back({name, tracers.back()->memoryBytes(), report.gpuMilliseconds});
    }

    Presenter presenter(context, window);
    Gui gui(context, window, presenter.format(), presenter.imageCount());
    ControlPanel panel(std::move(facts), core::OrbitCamera::framing(scene.bounds),
                       findTracer(tracerNames, options.tracers));
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

        const std::optional<VkCommandBuffer> cmd = presenter.beginFrame();
        if (!cmd) {
            gui.swapchainRebuilt();
            continue;
        }

        gui.newFrame();
        panel.handleInput();
        panel.draw();

        pipeline.begin(*cmd);
        pipeline.trace(*cmd, *tracers[panel.activeTracer()], panel.camera().frame(pipeline.aspectRatio()));
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
