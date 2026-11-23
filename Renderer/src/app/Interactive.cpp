#include "Interactive.h"

#include "Benchmark.h"
#include "CommandLine.h"
#include "ControlPanel.h"
#include "FramePipeline.h"
#include "Gui.h"
#include "PathText.h"
#include "Presenter.h"
#include "RecentFiles.h"
#include "SceneSetup.h"
#include "Screenshot.h"
#include "ShellFiles.h"
#include "TracerSetup.h"
#include "Window.h"
#include "WindowCommand.h"
#include "WindowSurface.h"

#include "thesis/gpu/Buffer.h"
#include "thesis/gpu/Context.h"
#include "thesis/gpu/Image.h"
#include "thesis/mesh/TriangleMesh.h"
#include "thesis/metrics/Sinks.h"
#include "thesis/profiling/Profiling.h"
#include "thesis/voxel/MeshImport.h"
#include "thesis/voxel/VoxelFile.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <system_error>
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
    // The voxel file that "Import mesh" wrote or "Open..." picked for this scene. It may not be in
    // the model list yet, so request.model means nothing until the window adds it. Empty: a
    // scene of the model list.
    std::filesystem::path addedFile;
    // Seconds to read the voxel file; none if the scene reused the model already in memory.
    std::optional<double> loadSeconds;
};

// What the worker thread does at the moment, for the status bar. The worker writes, the window reads.
class WorkerProgress {
public:
    explicit WorkerProgress(std::string step) : step_(std::move(step)) {}
    void set(std::string step)
    {
        const std::lock_guard lock(mutex_);
        step_ = std::move(step);
    }
    [[nodiscard]] std::string step() const
    {
        const std::lock_guard lock(mutex_);
        return step_;
    }
    [[nodiscard]] double seconds() const
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
    }
    // The share done of the step that reports one (voxelizing), and the switch to stop it.
    voxel::WorkMonitor monitor;
    std::atomic<bool> reportsShare{false}; // monitor.done means something now
    std::atomic<bool> cancellable{false};  // monitor.cancel still stops the work

private:
    mutable std::mutex mutex_;
    std::string step_;
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
};

// --self-test: a small sphere to import, written once per run.
struct SelfTestImport {
    std::filesystem::path mesh;
    std::filesystem::path directory; // where its voxel file goes, outside the model folder
    MeshImportRequest request;
    std::string expectedFile; // the voxel file name the import must open

    static SelfTestImport make()
    {
        SelfTestImport result;
        result.directory = std::filesystem::temp_directory_path() / "thesis_self_test";
        std::filesystem::create_directories(result.directory);
        // The images of an earlier run would let the "Save image" check pass without a save.
        for (const auto& entry : std::filesystem::directory_iterator(result.directory)) {
            if (utf8(entry.path().filename()).starts_with("self_test_image"))
                std::filesystem::remove(entry.path());
        }
        result.mesh = result.directory / "self_test_sphere.ply";
        mesh::savePly(result.mesh, mesh::makeSphere({0.0f, 0.0f, 0.0f}, 1.0f, 64));
        result.request = {result.mesh, {64, 32, voxel::VoxelizationRule::Sampled}, voxel::UpAxis::Z};
        result.expectedFile = voxel::voxelModelName(utf8(result.mesh.stem()), 64) + ".voxels";
        return result;
    }
};

// --self-test: the camera bookmark file. Kept cameras come back when the file is read again, and a
// damaged file is moved aside, so no later change writes over the user's cameras.
void checkBookmarkStorage(const std::filesystem::path& directory)
{
    const std::filesystem::path storage = directory / "self_test_cameras.json";
    std::filesystem::path aside = storage;
    aside.replace_extension(".damaged.json");
    std::filesystem::remove(storage);
    std::filesystem::remove(aside);

    const CameraBookmark own{"self test", 10.0f, 20.0f, 0.5f, glm::vec3(0.25f, 0.0f, 0.0f)};
    {
        CameraBookmarks written(storage);
        written.put(own);
    }
    const CameraBookmarks read(storage);
    const std::size_t expected = CameraBookmarks::defaults().size() + 1;
    if (!read.problem().empty() || read.list().size() != expected || read.list().back().name != own.name ||
        read.list().back().offset != own.offset)
        throw std::runtime_error("The camera bookmark file did not give back the cameras written to it");

    std::ofstream(storage) << "{ not json";
    const CameraBookmarks damaged(storage);
    if (damaged.problem().empty() || !std::filesystem::exists(aside) || std::filesystem::exists(storage) ||
        damaged.list().size() != CameraBookmarks::defaults().size())
        throw std::runtime_error("A damaged camera bookmark file was not moved aside with a problem shown");
    std::printf("Bookmark file check: %zu cameras read back; a damaged file was moved aside.\n", expected);
}

// The header of each voxel file of the model list. The headers only: a few small reads, also for
// large files.
std::vector<std::optional<voxel::VoxelFileSummary>> summariesOf(const std::vector<std::filesystem::path>& models)
{
    std::vector<std::optional<voxel::VoxelFileSummary>> summaries;
    for (const std::filesystem::path& model : models) {
        std::optional<voxel::VoxelFileSummary> summary;
        if (!model.empty()) {
            try {
                summary = voxel::readVoxelFileSummary(model);
            } catch (const std::exception&) {
                // A file that cannot be read shows its problem when the user opens it.
            }
        }
        summaries.push_back(std::move(summary));
    }
    return summaries;
}

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
// Between the models and that frame it imports a small mesh and checks that the window opens it.
class PanelTour {
public:
    PanelTour(const PanelFacts& facts, const SelfTestImport& import, bool& commandCheckDue)
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
        for (std::size_t i = 0; i < CameraBookmarks::defaults().size(); ++i) {
            steps_.push_back([i](ControlPanel& panel) { panel.selectBookmark(i); });
        }
        // A camera bookmark of its own: kept, picked, deleted (the self-test's bookmarks live in memory).
        steps_.push_back([](ControlPanel& panel) { panel.saveBookmark("self test"); });
        steps_.push_back([](ControlPanel& panel) {
            const std::vector<CameraBookmark>& list = panel.bookmarks().list();
            if (list.empty() || list.back().name != "self test")
                throw std::runtime_error("Keep view did not add the camera bookmark");
            panel.selectBookmark(list.size() - 1);
        });
        steps_.push_back([](ControlPanel& panel) { panel.removeBookmark(panel.bookmarks().list().size() - 1); });
        steps_.push_back([directory = import.directory](ControlPanel&) { checkBookmarkStorage(directory); });
        // The clock view, the warp maximum, the automatic ranges, the A/B image, the folding and
        // "Save image".
        steps_.push_back([](ControlPanel& panel) { panel.selectView(resolve::ShadingMode::WorkCycles); });
        steps_.push_back([](ControlPanel& panel) { panel.showWarpMaximum(true); });
        steps_.push_back([](ControlPanel& panel) {
            if (!panel.inspectCostliest(WorkCounter::Cycles))
                throw std::runtime_error("The cycles view got no work summary, so there is no costliest ray");
        });
        steps_.push_back([](ControlPanel& panel) { panel.stopInspecting(); });
        steps_.push_back([](ControlPanel& panel) { panel.showWarpMaximum(false); });
        steps_.push_back([](ControlPanel& panel) { panel.selectView(resolve::ShadingMode::Distance); });
        // The distance view with automatic ranges asks for the depth of the frame on screen.
        steps_.push_back([](ControlPanel& panel) {
            if (!panel.depthIsCurrent())
                throw std::runtime_error("The distance view got no depth, so its range cannot follow the surface");
            std::printf("Depth check: distance range %.1f to %.1f.\n", panel.shading().distanceNear,
                        panel.shading().distanceFar);
        });
        steps_.push_back([](ControlPanel& panel) { panel.setAutoRanges(false); });
        steps_.push_back([](ControlPanel& panel) { panel.setAutoRanges(true); });
        steps_.push_back([](ControlPanel& panel) {
            panel.compareInImage((panel.activeTracer() + 1) % panel.facts().tracers.size());
        });
        steps_.push_back([directory = import.directory](ControlPanel& panel) {
            panel.saveImage(directory / "self_test_image.png");
        });
        steps_.push_back([directory = import.directory](ControlPanel& panel) {
            if (!std::filesystem::is_regular_file(directory / "self_test_image.png") ||
                !std::filesystem::is_regular_file(directory / "self_test_image.txt"))
                throw std::runtime_error("Save image wrote no image or no command file");
            // With A/B on, the B image too: self_test_image_B_<tracer>.png.
            bool second = false;
            for (const auto& entry : std::filesystem::directory_iterator(directory))
                second = second || utf8(entry.path().filename()).starts_with("self_test_image_B_");
            if (!second)
                throw std::runtime_error("Save image with A/B on wrote no B image");
            std::printf("Save image check: image, B image and command written.\n");
            panel.compareInImage(std::nullopt);
        });
        // Two quick saves of one second get two names; a named save never writes over a command
        // file that belongs to no image of that name. The requests are taken here, so no file is written.
        steps_.push_back([directory = import.directory](ControlPanel& panel) {
            panel.saveImage(); // into the folder of the last image: the self-test's folder
            const std::optional<SaveImageRequest> first = panel.takeSaveImageRequest();
            if (!first || first->file.parent_path() != directory)
                throw std::runtime_error("Save (P) asked for no image in the folder of the last image");
            std::ofstream(first->file) << "taken";
            panel.saveImage();
            const std::optional<SaveImageRequest> second = panel.takeSaveImageRequest();
            std::filesystem::remove(first->file);
            if (!second || second->file == first->file)
                throw std::runtime_error("Two quick saves got the same file name");
            const std::filesystem::path notes = directory / "self_test_image_notes.txt";
            std::ofstream(notes) << "notes";
            panel.saveImage(directory / "self_test_image_notes.png");
            const bool refused = !panel.takeSaveImageRequest();
            std::filesystem::remove(notes);
            if (!refused)
                throw std::runtime_error("Save as would write over a text file that belongs to no image");
            panel.reportProblem(""); // the refusal's message, expected here
            std::printf("Save name check: second quick save got its own name; foreign .txt kept.\n");
        });
        steps_.push_back([](ControlPanel& panel) { panel.toggleImageOnly(); });
        steps_.push_back([](ControlPanel& panel) { panel.toggleImageOnly(); });
        steps_.push_back([](ControlPanel& panel) { panel.selectView(resolve::ShadingMode::Shaded); });
        // The next model of the list, and back.
        if (facts.models.size() > 1) {
            const std::size_t next = (facts.model + 1) % facts.models.size();
            steps_.push_back([next](ControlPanel& panel) { panel.selectModel(next); });
            steps_.push_back([start = facts.model](ControlPanel& panel) { panel.selectModel(start); });
        }
        // "Import mesh", then back to the first model. The import adds a model to the list, so the
        // first model is found by its file; a voxel file goes back through "Open...", which finds
        // it in the list.
        steps_.push_back([import](ControlPanel& panel) { panel.requestImport(import.request); });
        steps_.push_back([import, startFile = facts.models.at(facts.model)](ControlPanel& panel) {
            const PanelFacts& shown = panel.facts();
            if (utf8(shown.models.at(shown.model).filename()) != import.expectedFile || shown.voxelCount == 0)
                throw std::runtime_error("Import mesh did not open " + import.expectedFile);
            std::printf("Import check: %s opened, %llu voxels.\n", import.expectedFile.c_str(),
                        static_cast<unsigned long long>(shown.voxelCount));
            if (startFile.empty()) {
                panel.selectModel(shown.models.size() - 1); // the box field, always last
            } else {
                panel.requestOpen(startFile);
            }
        });
        steps_.push_back([startFile = facts.models.at(facts.model)](ControlPanel& panel) {
            const PanelFacts& shown = panel.facts();
            if (shown.models.at(shown.model) != startFile)
                throw std::runtime_error("Open did not return to " + utf8(startFile));
        });
        // "Remove from list" on the imported file: it leaves the list, the file stays, and the
        // model on screen is still the one shown. (Recycle is not run: it would fill the Recycle Bin.)
        const auto importedFile = [import](const PanelFacts& shown) {
            const auto it = std::find_if(shown.models.begin(), shown.models.end(), [&](const auto& model) {
                return utf8(model.filename()) == import.expectedFile;
            });
            return it == shown.models.end() ? std::filesystem::path{} : *it;
        };
        steps_.push_back([importedFile](ControlPanel& panel) {
            const std::filesystem::path file = importedFile(panel.facts());
            if (file.empty())
                throw std::runtime_error("The imported file is not in the model list");
            panel.requestModelListChange({ModelListRequest::Action::Remove, file});
        });
        steps_.push_back([importedFile, import, startFile = facts.models.at(facts.model)](ControlPanel& panel) {
            const PanelFacts& shown = panel.facts();
            if (!importedFile(shown).empty() || shown.models.at(shown.model) != startFile ||
                shown.modelSummaries.size() != shown.models.size() ||
                !std::filesystem::exists(import.directory / import.expectedFile))
                throw std::runtime_error("Remove from list did not take the imported file out of the list alone");
            std::printf("Model list check: %s removed from the list, file kept.\n", import.expectedFile.c_str());
        });

        // The frame of the command check: every setting away from its default where the scene allows it.
        steps_.push_back([](ControlPanel& panel) { panel.selectRayOrder(THESIS_RAY_ORDER_TILES); });
        for (const encoding::EncodingChoices::Switch& option : choices.switches()) {
            steps_.push_back(
                [option](ControlPanel& panel) { panel.selectEncodingSetting(option.setting, option.values.back()); });
        }
        steps_.push_back([](ControlPanel& panel) { panel.selectNextBrickSize(); });
        steps_.push_back([](ControlPanel& panel) { panel.selectBookmark(1); });
        // Fixed ranges: an automatic range could change between the frame and its check.
        steps_.push_back([](ControlPanel& panel) { panel.setAutoRanges(false); });
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
        steps_.push_back([](ControlPanel& panel) { panel.showWarpMaximum(true); });
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
    std::printf("Command check: %s\n", commandLine(utf8Generic(start.program), arguments).c_str());
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
    contextOptions.instanceExtensions = windowInstanceExtensions();
    contextOptions.createSurface = [&](VkInstance instance) { return createWindowSurface(window.handle(), instance); };
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

    // Where "Import mesh" writes: the model folder. The self-test writes elsewhere.
    std::optional<SelfTestImport> selfTestImport;
    if (options.selfTest)
        selfTestImport = SelfTestImport::make();
    const std::filesystem::path importDirectory =
        selfTestImport ? selfTestImport->directory : std::filesystem::path(THESIS_MODEL_DIR);
    // The model list grows by the files that "Import mesh" writes: before the box field.
    const auto addModel = [&](const std::filesystem::path& file) {
        for (std::size_t i = 0; i + 1 < models.size(); ++i) {
            std::error_code error;
            if (std::filesystem::equivalent(models[i], file, error))
                return i;
        }
        models.insert(models.end() - 1, std::filesystem::absolute(file).lexically_normal());
        return models.size() - 2;
    };

    // Builds the scene of voxel file `file` (empty: the box field) on the CPU. Safe on a worker
    // thread: it touches nothing but `source`, which the window does not use while a build runs.
    const auto buildScene = [options](std::shared_ptr<SceneSource> source, SceneRequest request,
                                      const std::filesystem::path& file) {
        if (file.empty()) {
            request.encoding.clear();
            request.brickSize = 0;
        }
        BuiltRequest result;
        if (!source || source->modelFile() != file) {
            Options sceneOptions = options;
            sceneOptions.model = file;
            sceneOptions.boxExample = file.empty();
            sceneOptions.encoding = request.encoding;
            sceneOptions.brickSize = request.brickSize;
            const auto start = std::chrono::steady_clock::now();
            source = std::make_shared<SceneSource>(sceneOptions);
            result.loadSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        }
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
        return commandLine(utf8Generic(options.program), commandArguments(optionsFor(options, state, form), form));
    };

    // Uploads a built scene and prepares every tracer for it, so that switching tracers is
    // instant. Fills `facts` with what the panel shows.
    double modelLoadSeconds = 0.0; // of the model in memory, kept while only the encoding changes
    const auto activate = [&](const BuiltRequest& loaded, PanelFacts& facts) {
        const BuiltScene& built = loaded.built;
        auto active = std::make_unique<ActiveScene>();
        active->gpuScene = tracing::GpuScene::upload(context, built.scene);

        facts = {};
        facts.models = models;
        facts.modelSummaries = summariesOf(models);
        facts.model = loaded.request.model;
        if (loaded.loadSeconds)
            modelLoadSeconds = *loaded.loadSeconds;
        facts.loadMilliseconds = 1e3 * modelLoadSeconds;
        facts.importDirectory = importDirectory;
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
        facts.warpSize = context.info().subgroupSize;
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
                     tracer->countsWork(), tracer->findsHits(), tracer->countsStructureSteps(), tracer->countsCycles(),
                     [plugin](const tracing::DiagramInput& input) { return plugin->diagram(input); },
                     [plugin](const core::RayLogEntry& step) { return plugin->describeStructureStep(step); },
                     tracer->warpsFollowRayList()});
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
        BuiltRequest first = buildScene(nullptr, loadedRequest, models.at(loadedRequest.model));
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
        tour.emplace(facts, *selfTestImport, commandCheckDue);
    }
    const auto [firstCamera, firstPreset] = startCameraOf(active->gpuScene.bounds, true);
    ControlPanel panel(std::move(facts), firstCamera, firstPreset,
                       startShading(options, firstCamera, context.info().subgroupSize), initialTracer);
    // A range given on the command line (e.g. from "Copy as command") is kept: the automatic
    // ranges would replace it with the first frame's.
    if (options.scaleMaximum || options.distanceRange.size() == 2)
        panel.setAutoRanges(false);
    if (options.inspect.size() == 2) {
        panel.inspectPixel(options.inspect[0], options.inspect[1]);
    }
    // The self-test leaves the user's list of recent files and camera bookmarks alone.
    panel.useRecentFiles(options.selfTest ? RecentFiles{} : RecentFiles(RecentFiles::defaultStorage()));
    panel.useCameraBookmarks(options.selfTest ? CameraBookmarks{} : CameraBookmarks(CameraBookmarks::defaultStorage()));
    // The frame of the right side of the A/B image: its own rays, hits and image, made when first used.
    std::unique_ptr<FramePipeline> splitPipeline;
    bool splitTraced = false; // the frame on its way traced the right side too
    // `splitWasTraced`: the frame that just finished also traced B into splitPipeline.
    const auto saveImages = [&](const SaveImageRequest& request, bool splitWasTraced) {
        if (!request.file.parent_path().empty())
            std::filesystem::create_directories(request.file.parent_path());
        const auto image = [&](FramePipeline& frame) {
            return resolve::RgbaImage{options.width, options.height, gpu::downloadImage(context, frame.output())};
        };
        writeFigure(image(*pipeline), panel.shading(), request.file);
        if (request.second && splitWasTraced && panel.splitTracer())
            writeFigure(image(*splitPipeline), panel.shadingFor(*panel.splitTracer()), *request.second);
        const std::filesystem::path text = request.commandFile();
        std::ofstream command(text);
        command << request.command << "\n";
        if (!command)
            throw std::runtime_error("cannot write " + utf8(text));
        panel.imageSaved(request.file);
    };
    metrics::TracySink tracySink;
    std::uint64_t frameIndex = 0;
    std::optional<std::future<BuiltRequest>> loading;
    std::shared_ptr<WorkerProgress> progress; // of `loading`
    int exitCode = 0;

    // Runs `job` on a worker thread; the window keeps running and shows its progress meanwhile.
    const auto startLoading = [&](std::string what, std::function<BuiltRequest(WorkerProgress&)> job) {
        panel.setLoading(what);
        progress = std::make_shared<WorkerProgress>(std::move(what));
        loading =
            std::async(std::launch::async, [job = std::move(job), progress = progress] { return job(*progress); });
    };

    while (!window.shouldClose()) {
        window.pollEvents();

        // The previous frame has finished on the GPU, so its timings can be read.
        if (presenter.waitForPreviousFrame()) {
            const std::vector<gpu::StageTiming> timings = pipeline->collectTimings();
            const bool splitWasTraced = std::exchange(splitTraced, false) && splitPipeline;
            if (splitWasTraced)
                (void)splitPipeline->collectTimings(); // not kept: the window times the frame of A only
            panel.setWindowFocused(window.focused() && !window.minimised());
            panel.recordTimings(timings);
            metrics::FrameSample sample{0, frameIndex, pipeline->batch().count(), {}};
            for (const gpu::StageTiming& timing : timings) {
                sample.stages.push_back({timing.stage, timing.milliseconds});
            }
            tracySink.recordFrame(sample);

            // "Save image" and the --self-test command check first: the frame that just finished
            // was shaded with the panel's shading of now. The work summary and the depth below
            // may change it (the automatic ranges) for the next frame.
            if (std::optional<SaveImageRequest> save = panel.takeSaveImageRequest()) {
                try {
                    saveImages(*save, splitWasTraced);
                } catch (const std::exception& error) {
                    panel.reportProblem(std::string("Save image: ") + error.what());
                }
            }
            if (std::exchange(commandCheckDue, false)) {
                const std::size_t differing = checkCommand(context, options, panel.windowState(), pipeline->output());
                std::printf("Command check: %zu of %u pixels differ from the window's frame.\n", differing,
                            options.width * options.height);
                if (differing != 0)
                    exitCode = 3;
            }

            // What the counting variant wrote in that frame: the steps of the inspected ray, and
            // (about once a second, while a work view is on) the average work of the rays.
            panel.recordRayLog(pipeline->rayLog());
            if (panel.wantsWorkSummary() && pipeline->work() != nullptr) {
                const std::size_t count = pipeline->batch().count();
                const std::vector<std::byte> workBytes = gpu::downloadBuffer(context, *pipeline->work());
                const std::vector<std::byte> hitBytes = gpu::downloadBuffer(context, pipeline->batch().hits);
                metrics::RayWorkSummary summary;
                summary.add({reinterpret_cast<const core::RayWork*>(workBytes.data()), count},
                            {reinterpret_cast<const core::Hit*>(hitBytes.data()), count},
                            panel.activeTracerWarps() ? context.info().subgroupSize : 0u);
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
            active = activate(buildScene(source, loadedRequest, models.at(loadedRequest.model)), restored);
            // The model and encoding are identical, so the inspector can retain its recorded ray.
            // The panel takes the new facts all the same: their hooks point at the new tracers.
            panel.sceneRebuilt(std::move(restored));
        }

        // A scene built on the worker thread is ready: free the old scene on the GPU (both would
        // not always fit), then upload the new one. If it fails, show why and build the old one again.
        if (loading && loading->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            std::future<BuiltRequest> done = std::move(*loading);
            loading.reset();
            progress.reset();
            panel.setLoading({});
            std::string problem;
            try {
                BuiltRequest built = done.get();
                // An imported file may replace the file on screen, with other voxels: always a new model.
                const bool imported = !built.addedFile.empty();
                if (imported) {
                    // The new file goes before the box field, which keeps its place at the end.
                    const bool boxFieldShown = loadedRequest.model + 1 == models.size();
                    built.request.model = addModel(built.addedFile);
                    if (boxFieldShown)
                        loadedRequest.model = models.size() - 1;
                }
                source = built.source;
                active.reset();
                PanelFacts newFacts;
                active = activate(built, newFacts);
                const bool otherModel = imported || built.request.model != loadedRequest.model;
                loadedRequest = built.request;
                panel.setFacts(std::move(newFacts));
                if (otherModel) {
                    const auto [camera, preset] = startCameraOf(active->gpuScene.bounds, false);
                    panel.resetCamera(camera, preset);
                }
            } catch (const voxel::Cancelled&) {
                problem = "Import cancelled. No voxel file was written.";
            } catch (const std::exception& error) {
                problem = error.what();
            }
            if (!problem.empty()) {
                // The model cannot be stored that way, does not fit, or cannot be read.
                std::fprintf(stderr, "error: %s\n", problem.c_str());
                if (!active) {
                    PanelFacts oldFacts;
                    active = activate(buildScene(source, loadedRequest, models.at(loadedRequest.model)), oldFacts);
                    panel.setFacts(std::move(oldFacts));
                } else {
                    panel.dropSceneChange();
                }
                panel.reportProblem(problem);
            }
        }

        // The user picked another model, encoding or brick size, or a mesh to import: build it on
        // the worker thread.
        // The right-click menu of the model list: a file leaves the list (and maybe the disk). The
        // scene on screen stays; only the indices behind it move.
        if (std::optional<ModelListRequest> change = loading ? std::nullopt : panel.takeModelListRequest()) {
            const auto it = std::find(models.begin(), models.end(), change->file);
            const auto index = static_cast<std::size_t>(it - models.begin());
            bool remove = it != models.end() && !it->empty() && index != loadedRequest.model;
            if (remove && change->action == ModelListRequest::Action::Recycle) {
                try {
                    if (!recycleFile(change->file)) {
                        remove = false; // the user kept the file when Windows asked
                    }
                } catch (const std::exception& error) {
                    panel.reportProblem(error.what());
                    remove = false;
                }
            }
            if (remove) {
                models.erase(it);
                if (loadedRequest.model > index)
                    --loadedRequest.model;
                panel.setModelList(models, summariesOf(models), loadedRequest.model);
            }
        }

        if (!loading) {
            const PanelFacts& shown = panel.facts();
            SceneRequest wanted{panel.wantedModel(), {}, panel.wantedBrickSize()};
            if (!shown.encodings.empty())
                wanted.encoding = shown.encodings.at(panel.wantedEncoding()).name;
            if (std::optional<MeshImportRequest> import = panel.takeImportRequest()) {
                // Voxelize, write the voxel file, then build its scene with the encoding and brick
                // size on screen. A fresh source: the file may replace the one on screen.
                const std::string meshName = utf8(import->mesh.filename());
                startLoading("Importing " + meshName, [buildScene, wanted, meshName, import = *import,
                                                       directory = importDirectory](WorkerProgress& progress) {
                    const voxel::MeshImportOptions importOptions{import.voxelize, import.upAxis, {}, directory};
                    const std::string target = utf8(voxel::importTarget(import.mesh, importOptions).filename());
                    // Cancel works until the file is written; reading the mesh has no share to show.
                    progress.cancellable = true;
                    const voxel::MeshImportReport report = voxel::importMesh(
                        import.mesh, importOptions,
                        [&](voxel::ImportStep step) {
                            switch (step) {
                            case voxel::ImportStep::ReadMesh:
                                progress.set("Reading " + meshName);
                                break;
                            case voxel::ImportStep::Voxelize:
                                progress.reportsShare = true;
                                progress.set(std::format("Voxelizing {} at {}", meshName, import.voxelize.resolution));
                                break;
                            case voxel::ImportStep::WriteFile:
                                progress.reportsShare = false;
                                progress.cancellable = false;
                                progress.set("Writing " + target);
                                break;
                            }
                        },
                        &progress.monitor);
                    progress.set("Loading " + target);
                    BuiltRequest result = buildScene(nullptr, wanted, report.output);
                    result.addedFile = report.output;
                    return result;
                });
            } else if (std::optional<std::filesystem::path> opened = panel.takeOpenRequest()) {
                // "Open...": a voxel file from any folder, with the encoding and brick size on screen.
                startLoading("Loading " + utf8(opened->filename()), [buildScene, wanted, opened](WorkerProgress&) {
                    BuiltRequest result = buildScene(nullptr, wanted, *opened);
                    result.addedFile = *opened;
                    return result;
                });
            } else if (wanted.model != loadedRequest.model || wanted.encoding != loadedRequest.encoding ||
                       wanted.brickSize != loadedRequest.brickSize) {
                const std::filesystem::path file = models.at(wanted.model);
                startLoading(
                    wanted.model != loadedRequest.model
                        ? "Loading " + (file.empty() ? std::string("box field") : utf8(file.filename()))
                        : "Encoding " + shown.sceneName,
                    [buildScene, source, wanted, file](WorkerProgress&) { return buildScene(source, wanted, file); });
            }
        }
        if (progress) {
            if (panel.takeCancelRequest())
                progress->monitor.cancel = true;
            const bool share = progress->reportsShare.load();
            const float done = progress->monitor.done.load();
            panel.setLoading(
                share ? std::format("{} {:.0f}% ({:.0f} s)", progress->step(), 100.0f * done, progress->seconds())
                      : std::format("{} ({:.0f} s)", progress->step(), progress->seconds()));
            panel.setLoadingProgress(share ? std::optional<float>(done) : std::nullopt,
                                     progress->cancellable.load() && !progress->monitor.cancel.load());
        }

        // Another ray order: a new frame pipeline. The previous frame has finished, so the old one can go.
        if (panel.wantedRayOrder() != static_cast<std::uint32_t>(pipeline->batch().order)) {
            pipeline.reset();
            splitPipeline.reset();
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

        // After the panel: the keys and clicks of this frame may switch the A/B image, its tracer or
        // the view, and the frame must trace what the panel shows (its split line, FrameInFlight).
        const std::optional<std::size_t> splitTracer = panel.splitTracer();
        if (splitTracer && !splitPipeline) {
            splitPipeline = std::make_unique<FramePipeline>(
                context, options.shaderDirectory, VkExtent2D{options.width, options.height}, pipeline->batch().order);
        }

        const core::CameraFrame cameraFrame = panel.camera().frame(pipeline->aspectRatio());
        pipeline->begin(*cmd);
        pipeline->setLoggedRay(panel.loggedRay());
        pipeline->trace(*cmd, *active->tracers[panel.activeTracer()], cameraFrame, panel.countWork());
        pipeline->shade(*cmd, panel.shading());
        // The A/B image: the second tracer traces the same camera rays into its own frame.
        std::optional<Presenter::Split> split;
        if (splitTracer && splitPipeline) {
            splitPipeline->begin(*cmd);
            splitPipeline->trace(*cmd, *active->tracers[*splitTracer], cameraFrame, panel.countWorkFor(*splitTracer));
            splitPipeline->shade(*cmd, panel.shadingFor(*splitTracer));
            split.emplace(Presenter::Split{splitPipeline->output(), panel.splitAt()});
        }
        const bool rebuilt = presenter.present(
            pipeline->output(), panel.imageArea(),
            [&](VkCommandBuffer overlayCmd, VkImageView view, VkExtent2D extent) {
                gui.render(overlayCmd, view, extent);
            },
            split);
        splitTraced = split.has_value();
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
