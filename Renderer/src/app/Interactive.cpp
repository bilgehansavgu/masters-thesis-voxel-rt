#include "Interactive.h"

#include "Benchmark.h"
#include "CommandLine.h"
#include "ControlPanel.h"
#include "FramePipeline.h"
#include "Gui.h"
#include "Presenter.h"
#include "RayWorkSummary.h"
#include "SceneSetup.h"
#include "Screenshot.h"
#include "Window.h"
#include "WindowCommand.h"

#include "thesis/gpu/Buffer.h"
#include "thesis/gpu/Context.h"
#include "thesis/gpu/Image.h"
#include "thesis/metrics/Sinks.h"
#include "thesis/profiling/Profiling.h"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>

namespace thesis::app {
namespace {

// Everything that exists once per built scene. Rebuilt when the user picks another model,
// encoding or brick size.
struct ActiveScene {
    tracing::GpuScene gpuScene; // declared before the tracers: they point at it and are destroyed first
    std::vector<std::unique_ptr<tracing::ITracer>> tracers;
};

// The scene the user asked for.
struct SceneRequest {
    std::size_t model = 0;       // index into the model list
    std::string encoding;        // empty: the default encoding (or the box field)
    std::uint32_t brickSize = 0; // 0: as in the voxel file
    bool operator==(const SceneRequest&) const = default;
};

// A scene built on the CPU, ready to go to the GPU. Built on a worker thread, so the window
// stays alive while a large model loads and encodes.
struct BuiltRequest {
    SceneRequest request;
    std::shared_ptr<SceneSource> source;
    std::size_t encoding = 0;
    BuiltScene built;
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
// "Quick timing", shows each view, then goes through the encodings, the brick sizes, the tracers,
// the encoding switches, the ray orders, the camera presets and the models. At the end it sets
// a scene far from the defaults and asks the caller to check "Copy as command" against it.
class PanelTour {
public:
    PanelTour(const PanelFacts& facts, bool& commandCheckDue)
    {
        steps_.push_back([](ControlPanel& panel) { panel.measureAll(); });
        for (std::size_t mode = 0; mode < resolve::kShadingModeNames.size(); ++mode) {
            steps_.push_back(
                [mode](ControlPanel& panel) { panel.selectView(static_cast<resolve::ShadingMode>(mode)); });
        }
        steps_.push_back([](ControlPanel& panel) { panel.selectView(resolve::ShadingMode::Shaded); });
        steps_.insert(steps_.end(), facts.encodings.size(), [](ControlPanel& panel) { panel.selectNextEncoding(); });
        steps_.insert(steps_.end(), facts.brickSizes.size(), [](ControlPanel& panel) { panel.selectNextBrickSize(); });
        // The structure overlay and the ray walk on the ray in the middle of the frame, for every
        // tracer (a tracer switch records the same pixel again), then off again.
        steps_.push_back([](ControlPanel& panel) { panel.showStructure(true); });
        steps_.push_back([width = facts.width, height = facts.height](ControlPanel& panel) {
            panel.inspectPixel(width / 2, height / 2);
        });
        for (std::size_t i = 0; i < facts.tracers.size(); ++i) {
            steps_.push_back([](ControlPanel& panel) { panel.nextRayStep(); });
            steps_.push_back([](ControlPanel& panel) { panel.selectNextTracer(); });
        }
        // Forward into the brick stages, the camera to the brick, then back across stage borders.
        steps_.insert(steps_.end(), 64, [](ControlPanel& panel) { panel.nextRayStep(); });
        steps_.push_back([](ControlPanel& panel) { panel.showBrick(); });
        steps_.insert(steps_.end(), 64, [](ControlPanel& panel) { panel.previousRayStep(); });
        steps_.push_back([](ControlPanel& panel) { panel.stopInspecting(); });
        steps_.push_back([](ControlPanel& panel) { panel.showStructure(false); });

        // Every value of every encoding switch, then back to the first values.
        const encoding::EncodingChoices& choices = facts.encodingChoices;
        for (const encoding::EncodingChoices::Switch& option : choices.switches()) {
            for (const std::string& value : option.values) {
                steps_.push_back(
                    [option, value](ControlPanel& panel) { panel.selectEncodingSetting(option.setting, value); });
            }
            steps_.push_back(
                [option](ControlPanel& panel) { panel.selectEncodingSetting(option.setting, option.values.front()); });
        }
        steps_.push_back([](ControlPanel& panel) { panel.selectRayOrder(THESIS_RAY_ORDER_TILES); });
        steps_.push_back([](ControlPanel& panel) { panel.selectRayOrder(THESIS_RAY_ORDER_ROWS); });
        for (std::size_t i = 0; i < kCameraPresets.size(); ++i) {
            steps_.push_back([i](ControlPanel& panel) { panel.selectCameraPreset(i); });
        }
        // The next model of the list, and back.
        if (facts.models.size() > 1) {
            const std::size_t next = (facts.model + 1) % facts.models.size();
            steps_.push_back([next](ControlPanel& panel) { panel.selectModel(next); });
            steps_.push_back([start = facts.model](ControlPanel& panel) { panel.selectModel(start); });
        }

        // The frame of the command check: every setting away from its default where the scene allows it.
        steps_.push_back([](ControlPanel& panel) { panel.selectRayOrder(THESIS_RAY_ORDER_TILES); });
        for (const encoding::EncodingChoices::Switch& option : choices.switches()) {
            steps_.push_back(
                [option](ControlPanel& panel) { panel.selectEncodingSetting(option.setting, option.values.back()); });
        }
        steps_.push_back([](ControlPanel& panel) { panel.selectNextBrickSize(); });
        steps_.push_back([](ControlPanel& panel) { panel.selectCameraPreset(1); });
        // Another tracer that finds hits and counts work, so the frame has a surface and a work view.
        steps_.push_back([](ControlPanel& panel) {
            const std::vector<PanelFacts::Tracer>& tracers = panel.facts().tracers;
            for (std::size_t i = 0; i < tracers.size(); ++i) {
                if (i != panel.activeTracer() && tracers[i].findsHits && tracers[i].countsWork) {
                    panel.selectTracer(i);
                    break;
                }
            }
        });
        steps_.push_back([](ControlPanel& panel) { panel.selectView(resolve::ShadingMode::WorkPrimitives); });
        steps_.push_back([](ControlPanel& panel) { panel.copyCommand(CommandForm::Benchmark); });
        steps_.push_back([](ControlPanel& panel) { panel.copyCommand(CommandForm::Screenshot); });
        // No change in this step: the frame on screen belongs to the state of the panel.
        steps_.push_back([&commandCheckDue](ControlPanel&) { commandCheckDue = true; });
    }

    // Call once for each frame. Returns false when the tour is over.
    bool advance(ControlPanel& panel)
    {
        if (panel.measuring() || panel.sceneChangePending() || ++framesSinceStep_ < kFramesPerStep) {
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

// Renders `state` again from its screenshot command, the way a user would run it, and compares
// it with the frame on screen. Returns the number of pixels that differ.
std::size_t checkCommand(const gpu::Context& context, const Options& start, const WindowState& state,
                         gpu::StorageImage& onScreen)
{
    const std::vector<std::string> arguments =
        commandArguments(optionsFor(start, state, CommandForm::Screenshot), CommandForm::Screenshot);
    std::printf("Command check: %s\n", commandLine(start.program.generic_string(), arguments).c_str());
    ParsedCommand parsed = parseCommandLine(arguments);
    if (parsed.exitCode) {
        throw std::runtime_error("The command of the window does not parse");
    }
    parsed.options.shaderDirectory = start.shaderDirectory;
    const RenderedFrame frame = renderFrame(context, parsed.options);
    const std::vector<std::byte> window = gpu::downloadImage(context, onScreen);
    if (window.size() != frame.image.pixels.size()) {
        throw std::runtime_error("The command renders a frame of another size");
    }
    std::size_t differing = 0;
    for (std::size_t pixel = 0; pixel < window.size(); pixel += 4) {
        differing += std::equal(window.begin() + static_cast<std::ptrdiff_t>(pixel),
                                window.begin() + static_cast<std::ptrdiff_t>(pixel + 4),
                                frame.image.pixels.begin() + static_cast<std::ptrdiff_t>(pixel))
                         ? 0u
                         : 1u;
    }
    return differing;
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

    // The scenes to choose from: every voxel file of the model folder, a file named with --model,
    // then the box field (empty path).
    std::vector<std::filesystem::path> models = listModels(THESIS_MODEL_DIR);
    const std::filesystem::path commandModel =
        options.model.empty() ? std::filesystem::path{} : std::filesystem::absolute(options.model);
    if (!commandModel.empty() && std::none_of(models.begin(), models.end(), [&](const auto& path) {
            return std::filesystem::equivalent(path, commandModel);
        })) {
        models.push_back(commandModel);
    }
    models.emplace_back();
    const auto modelIndex = [&](const std::filesystem::path& path) {
        for (std::size_t i = 0; i + 1 < models.size(); ++i) {
            if (!path.empty() && std::filesystem::equivalent(models[i], path))
                return i;
        }
        return models.size() - 1;
    };
    std::filesystem::path firstModel = commandModel;
    if (firstModel.empty() && !options.boxExample && std::filesystem::is_regular_file(THESIS_LUCY_MODEL)) {
        firstModel = THESIS_LUCY_MODEL;
    }

    // Builds a scene on the CPU. Safe on a worker thread: it touches nothing but `source`, which
    // the window does not use while a build runs.
    const auto buildScene = [models, options](std::shared_ptr<SceneSource> source, SceneRequest request) {
        const std::filesystem::path& file = models.at(request.model);
        if (file.empty()) {
            request.encoding.clear();
            request.brickSize = 0;
        }
        if (!source || source->modelFile() != file) {
            Options sceneOptions = options;
            sceneOptions.model = file;
            sceneOptions.boxExample = file.empty();
            sceneOptions.encoding = request.encoding;
            sceneOptions.brickSize = request.brickSize;
            source = std::make_shared<SceneSource>(sceneOptions);
        }
        BuiltRequest result;
        result.encoding = !source->isVoxelModel()    ? 0
                          : request.encoding.empty() ? source->initialEncoding()
                                                     : source->encodingIndex(request.encoding);
        const std::uint32_t brickSize = request.brickSize != 0 ? request.brickSize : source->initialBrickSize();
        result.built = source->build(result.encoding, brickSize);
        result.request = request;
        result.request.brickSize = result.built.brickSize;
        if (source->isVoxelModel())
            result.request.encoding = source->encodings().at(result.encoding).name;
        result.source = std::move(source);
        return result;
    };

    const tracing::TracerRegistry registry = makeTracerRegistry();
    const tracing::TracerEnvironment environment{context, options.shaderDirectory};
    auto pipeline =
        std::make_unique<FramePipeline>(context, options.shaderDirectory, VkExtent2D{options.width, options.height},
                                        static_cast<tracing::RayOrder>(options.rayOrder));
    const auto commandWriter = [options](const WindowState& state, CommandForm form) {
        return commandLine(options.program.generic_string(), commandArguments(optionsFor(options, state, form), form));
    };

    // Uploads a built scene and prepares every tracer for it, so that switching tracers is
    // instant. Fills `facts` with what the panel shows.
    const auto activate = [&](const BuiltRequest& loaded, PanelFacts& facts) {
        const BuiltScene& built = loaded.built;
        auto active = std::make_unique<ActiveScene>();
        active->gpuScene = tracing::GpuScene::upload(context, built.scene);

        facts = {};
        facts.models = models;
        facts.model = loaded.request.model;
        for (const SceneSource::EncodingChoice& choice : loaded.source->encodings()) {
            facts.encodings.push_back({choice.name, choice.description, choice.skipsEmptySpace, choice.skippingTwin});
        }
        facts.encoding = loaded.encoding;
        facts.encodingChoices = loaded.source->encodingChoices();
        facts.brickSizes = loaded.source->brickSizes();
        facts.brickSize = built.brickSize;
        facts.sceneName = built.scene.name;
        facts.sceneDetails = built.details;
        facts.sceneBounds = built.scene.bounds;
        facts.geometry.bounds = built.scene.primitiveBounds;
        facts.geometry.grid = built.scene.primitiveGrid;
        facts.geometry.brickSize = built.brickSize;
        if (built.model) {
            // The ray walk reads the voxels of one brick at a time from the model in memory.
            facts.geometry.voxels = [model = built.model,
                                     size = built.brickSize](glm::uvec3 coordinate) -> std::optional<BrickVoxels> {
                const voxel::VoxelBrick* brick = findBrick(*model, coordinate);
                if (!brick)
                    return std::nullopt;
                BrickVoxels voxels;
                voxels.size = size;
                voxels.bits = brick->occupancy;
                for (std::uint32_t z = 0; z < size; ++z)
                    for (std::uint32_t y = 0; y < size; ++y)
                        for (std::uint32_t x = 0; x < size; ++x)
                            if (voxels.isFull(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z)))
                                voxels.full.emplace_back(x, y, z);
                return voxels;
            };
        }
        facts.geometry.bricks = built.bricks;
        facts.primitiveCount = active->gpuScene.primitiveCount;
        facts.voxelCount = built.voxels;
        for (const encoding::MemoryReport::Buffer& buffer : built.memory.buffers) {
            facts.sceneBuffers.push_back({buffer.name, buffer.bytes});
        }
        facts.sceneBytes = active->gpuScene.memoryBytes();
        facts.encodeMilliseconds = 1e3 * built.encodeSeconds;
        facts.width = options.width;
        facts.height = options.height;
        facts.rayOrder = static_cast<std::uint32_t>(pipeline->batch().order);
        facts.rayAndHitBytes = pipeline->batch().memoryBytes();
        facts.command = commandWriter;

        for (const auto& entry : registry.entries()) {
            auto tracer = registry.create(entry.name, environment);
            try {
                const tracing::PrepareReport report = tracer->prepare(active->gpuScene);
                const tracing::TracerText text = tracer->text();
                const tracing::ITracer* plugin = tracer.get();
                facts.tracers.push_back(
                    {entry.name, entry.description, text.label, text.method, text.structureStep, tracer->memoryBytes(),
                     report.gpuMilliseconds + report.cpuMilliseconds, report.cpuMilliseconds > 0.0,
                     tracer->countsWork(), tracer->findsHits(), tracer->countsStructureSteps(),
                     [plugin](const tracing::DiagramInput& input) { return plugin->diagram(input); },
                     [plugin](const core::RayLogEntry& step) { return plugin->describeStructureStep(step); }});
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

    // The camera a model starts with: --camera, or the exact camera of the command line for the
    // first scene only.
    const auto startCameraOf = [&](const core::Bounds& bounds, bool first) {
        Options cameraOptions = options;
        if (!first)
            cameraOptions.exactCamera.reset();
        const std::optional<std::array<float, 3>> preset =
            cameraOptions.exactCamera ? std::nullopt
                                      : std::optional<std::array<float, 3>>(std::array<float, 3>{
                                            options.camera[0], options.camera[1], options.camera[2]});
        return std::pair{startCamera(cameraOptions, bounds), preset};
    };

    PanelFacts facts;
    SceneRequest loadedRequest{modelIndex(firstModel), options.encoding, options.brickSize};
    std::shared_ptr<SceneSource> source;
    std::unique_ptr<ActiveScene> active;
    {
        BuiltRequest first = buildScene(nullptr, loadedRequest);
        loadedRequest = first.request;
        source = first.source;
        active = activate(first, facts);
    }
    for (const std::string& skipped : facts.skippedTracers) {
        std::printf("note: tracer not available for this scene: %s\n", skipped.c_str());
    }

    Presenter presenter(context, window);
    Gui gui(context, window, presenter.format(), presenter.imageCount());
    const std::size_t initialTracer = findTracer(facts.tracers, options.tracers, registry.defaultName());
    bool commandCheckDue = false;
    std::optional<PanelTour> tour;
    if (options.selfTest) {
        tour.emplace(facts, commandCheckDue);
    }
    const auto [firstCamera, firstPreset] = startCameraOf(active->gpuScene.bounds, true);
    ControlPanel panel(std::move(facts), firstCamera, firstPreset, startShading(options, firstCamera), initialTracer);
    if (options.inspect.size() == 2) {
        panel.inspectPixel(options.inspect[0], options.inspect[1]);
    }
    metrics::TracySink tracySink;
    std::uint64_t frameIndex = 0;
    std::optional<std::future<BuiltRequest>> loading;
    int exitCode = 0;

    // Builds `request` on a worker thread; the window keeps running and shows `what` meanwhile.
    const auto startLoading = [&](SceneRequest request, std::string what) {
        panel.setLoading(std::move(what));
        loading = std::async(std::launch::async, buildScene, source, std::move(request));
    };

    while (!window.shouldClose()) {
        window.pollEvents();

        // The previous frame has finished on the GPU, so its timings can be read.
        if (presenter.waitForPreviousFrame()) {
            const std::vector<gpu::StageTiming> timings = pipeline->collectTimings();
            panel.setWindowFocused(window.focused() && !window.minimised());
            panel.recordTimings(timings);
            metrics::FrameSample sample{0, frameIndex, pipeline->batch().count(), {}};
            for (const gpu::StageTiming& timing : timings) {
                sample.stages.push_back({timing.stage, timing.milliseconds});
            }
            tracySink.recordFrame(sample);

            // What the counting variant wrote in that frame: the steps of the inspected ray, and
            // (about once a second, while a work view is on) the average work of the rays.
            panel.recordRayLog(pipeline->rayLog());
            if (panel.wantsWorkSummary() && pipeline->work() != nullptr) {
                const std::size_t count = pipeline->batch().count();
                const std::vector<std::byte> workBytes = gpu::downloadBuffer(context, *pipeline->work());
                const std::vector<std::byte> hitBytes = gpu::downloadBuffer(context, pipeline->batch().hits);
                RayWorkSummary summary;
                summary.add({reinterpret_cast<const core::RayWork*>(workBytes.data()), count},
                            {reinterpret_cast<const core::Hit*>(hitBytes.data()), count});
                panel.recordWorkSummary(summary);
            }
            // Once for each camera position: the distance to the surface of each pixel, so that the
            // overlay can draw lines behind the model faint.
            if (panel.wantsDepth()) {
                const std::vector<std::byte> hitBytes = gpu::downloadBuffer(context, pipeline->batch().hits);
                panel.recordDepth(
                    DepthMap::fromHits({reinterpret_cast<const core::Hit*>(hitBytes.data()), pipeline->batch().count()},
                                       pipeline->batch().width, pipeline->batch().height,
                                       static_cast<std::uint32_t>(pipeline->batch().order)));
            }
            // --self-test: the frame that just finished belongs to the panel state of now.
            if (std::exchange(commandCheckDue, false)) {
                const std::size_t differing = checkCommand(context, options, panel.windowState(), pipeline->output());
                std::printf("Command check: %zu of %u pixels differ from the window's frame.\n", differing,
                            options.width * options.height);
                if (differing != 0)
                    exitCode = 3;
            }
        }

        // Not while a scene is built: the comparison builds the scene again from the same source.
        if (auto request = loading ? std::nullopt : panel.takeComparisonRequest()) {
            Options measured = options;
            measured.model = models.at(loadedRequest.model);
            measured.boxExample = measured.model.empty();
            measured.exactCamera = panel.camera();
            measured.tracers = {panel.tracerName()};
            measured.encoding = loadedRequest.encoding;
            measured.brickSize = loadedRequest.brickSize;
            measured.rayOrder = static_cast<std::uint32_t>(pipeline->batch().order);
            active.reset();
            // Keep processing window events while the interactive renderer submits no GPU work.
            try {
                auto worker = std::async(std::launch::async, compareView, std::move(measured), *request);
                // Other programs in front can use the GPU and change its clock. Note it if that happened.
                bool alwaysInFront = window.focused();
                while (worker.wait_for(std::chrono::milliseconds(50)) != std::future_status::ready) {
                    window.pollEvents();
                    alwaysInFront = alwaysInFront && window.focused();
                }
                panel.showComparison(worker.get());
                if (!alwaysInFront)
                    panel.reportProblem("The window was not in front during the whole comparison: other programs may "
                                        "have used the GPU. Run it again without using the computer.");
            } catch (const std::exception& error) {
                panel.reportProblem(error.what());
            }
            PanelFacts restored;
            active = activate(buildScene(source, loadedRequest), restored);
            // The model and encoding are identical, so the inspector can retain its recorded ray.
        }

        // A scene built on the worker thread is ready: free the old scene on the GPU (both would
        // not always fit), then upload the new one. If it fails, show why and build the old one again.
        if (loading && loading->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            std::future<BuiltRequest> done = std::move(*loading);
            loading.reset();
            panel.setLoading({});
            std::string problem;
            try {
                BuiltRequest built = done.get();
                source = built.source;
                active.reset();
                PanelFacts newFacts;
                active = activate(built, newFacts);
                const bool otherModel = built.request.model != loadedRequest.model;
                loadedRequest = built.request;
                panel.setFacts(std::move(newFacts));
                if (otherModel) {
                    const auto [camera, preset] = startCameraOf(active->gpuScene.bounds, false);
                    panel.resetCamera(camera, preset);
                }
            } catch (const std::exception& error) {
                problem = error.what();
            }
            if (!problem.empty()) {
                // The model cannot be stored that way, does not fit, or cannot be read.
                std::fprintf(stderr, "error: %s\n", problem.c_str());
                if (!active) {
                    PanelFacts oldFacts;
                    active = activate(buildScene(source, loadedRequest), oldFacts);
                    panel.setFacts(std::move(oldFacts));
                } else {
                    panel.dropSceneChange();
                }
                panel.reportProblem(problem);
            }
        }

        // The user picked another model, encoding or brick size: build it on the worker thread.
        if (!loading) {
            const PanelFacts& shown = panel.facts();
            SceneRequest wanted{panel.wantedModel(), {}, panel.wantedBrickSize()};
            if (!shown.encodings.empty())
                wanted.encoding = shown.encodings.at(panel.wantedEncoding()).name;
            if (wanted.model != loadedRequest.model || wanted.encoding != loadedRequest.encoding ||
                wanted.brickSize != loadedRequest.brickSize) {
                const std::filesystem::path& file = models.at(wanted.model);
                startLoading(wanted,
                             wanted.model != loadedRequest.model
                                 ? "Loading " + (file.empty() ? std::string("box field") : file.filename().string())
                                 : "Encoding " + shown.sceneName);
            }
        }

        // Another ray order: a new frame pipeline. The previous frame has finished, so the old one can go.
        if (panel.wantedRayOrder() != static_cast<std::uint32_t>(pipeline->batch().order)) {
            pipeline.reset();
            pipeline = std::make_unique<FramePipeline>(context, options.shaderDirectory,
                                                       VkExtent2D{options.width, options.height},
                                                       static_cast<tracing::RayOrder>(panel.wantedRayOrder()));
            panel.rayOrderChanged(static_cast<std::uint32_t>(pipeline->batch().order));
        }

        const std::optional<VkCommandBuffer> cmd = presenter.beginFrame();
        if (!cmd) {
            gui.swapchainRebuilt();
            continue;
        }

        gui.newFrame();
        panel.handleInput();
        panel.draw();

        pipeline->begin(*cmd);
        pipeline->setLoggedRay(panel.loggedRay());
        pipeline->trace(*cmd, *active->tracers[panel.activeTracer()], panel.camera().frame(pipeline->aspectRatio()),
                        panel.countWork());
        pipeline->shade(*cmd, panel.shading());
        const bool rebuilt = presenter.present(pipeline->output(), panel.imageArea(),
                                               [&](VkCommandBuffer overlayCmd, VkImageView view, VkExtent2D extent) {
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
    if (loading) {
        loading->wait(); // the worker uses the scene source; let it finish before the program ends
    }
    if (options.validation && gpu::validationMessageCount() > 0) {
        std::fprintf(stderr, "%llu validation message(s) were reported.\n",
                     static_cast<unsigned long long>(gpu::validationMessageCount()));
        return 2;
    }
    return exitCode;
}

} // namespace thesis::app
