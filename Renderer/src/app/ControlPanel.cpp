// The state of the panel, the input on the image, the timing of the frames, and the dock layout.
// The windows are drawn in ControlPanelSettings.cpp, ControlPanelInspector.cpp,
// ControlPanelMeasurements.cpp and ControlPanelStatusBar.cpp.

#include "ControlPanel.h"

#include "FileDialog.h"
#include "FramePipeline.h"
#include "PanelWidgets.h"
#include "PathText.h"

#include "thesis/core/Statistics.h"
#include "thesis/resolve/ViewSupport.h"
#include "thesis/shared/RayLog.h"
#include "thesis/shared/RayOrder.h"

#include <glm/geometric.hpp>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <format>
#include <limits>
#include <span>
#include <string_view>
#include <utility>

namespace thesis::app {
namespace {

using resolve::ShadingMode;

// The names of the windows: also the keys of the dock layout.
constexpr const char* kSettingsWindow = "Settings";
constexpr const char* kInspectorWindow = "Inspector";
constexpr const char* kMeasurementsWindow = "Measurements";

} // namespace

ControlPanel::ControlPanel(PanelFacts facts, const core::OrbitCamera& initialCamera,
                           std::optional<std::array<float, 3>> initialPreset,
                           const resolve::ShadingParams& initialShading, std::size_t initialTracer)
    : facts_(std::move(facts)),
      initialCamera_(initialCamera),
      initialPreset_(initialPreset),
      camera_(initialCamera),
      cameraPreset_(initialPreset),
      shading_(initialShading),
      activeTracer_(initialTracer),
      wantedModel_(facts_.model),
      wantedEncoding_(facts_.encoding),
      wantedBrickSize_(facts_.brickSize),
      wantedRayOrder_(facts_.rayOrder),
      traceTimes_(facts_.tracers.size())
{
    if (unavailableView(shading_.mode))
        shading_.mode = ShadingMode::Shaded;
    if (const std::optional<WorkCounter> counter = counterOf(shading_.mode))
        histogramCounter_ = *counter;
    rememberEncodingSize();
}

void ControlPanel::resetCamera(const core::OrbitCamera& initialCamera,
                               std::optional<std::array<float, 3>> initialPreset)
{
    initialCamera_ = initialCamera;
    initialPreset_ = initialPreset;
    returnCamera();
}

void ControlPanel::returnCamera()
{
    stopInspecting();
    camera_ = initialCamera_;
    cameraMoved();
    cameraPreset_ = initialPreset_;
}

WindowState ControlPanel::windowState() const
{
    WindowState state;
    state.model = facts_.models.at(facts_.model);
    if (!facts_.encodings.empty()) {
        state.encoding = facts_.encodings.at(facts_.encoding).name;
        state.brickSize = facts_.brickSize;
    }
    state.tracer = facts_.tracers.at(activeTracer_).name;
    state.rayOrder = facts_.rayOrder;
    state.camera = camera_;
    state.cameraPreset = cameraPreset_;
    state.shading = shading();
    state.inspectedPixel = inspectedPixel_;
    return state;
}

PixelRect ControlPanel::imageArea() const
{
    return framebufferRect(imageMin_, imageMax_);
}

void ControlPanel::selectModel(std::size_t index)
{
    if (index < facts_.models.size() && !measuring() && loading_.empty()) {
        wantedModel_ = index;
        problem_.clear();
    }
}

void ControlPanel::requestImport(MeshImportRequest request)
{
    if (!measuring() && loading_.empty()) {
        importRequest_ = std::move(request);
        problem_.clear();
        rememberFile("meshes", importRequest_->mesh);
    }
}

void ControlPanel::requestOpen(std::filesystem::path voxelFile)
{
    if (!measuring() && loading_.empty() && !voxelFile.empty()) {
        openRequest_ = std::move(voxelFile);
        problem_.clear();
    }
}

void ControlPanel::requestModelListChange(ModelListRequest request)
{
    const std::filesystem::path& shown = facts_.models.at(facts_.model);
    const std::filesystem::path& wanted = facts_.models.at(wantedModel_);
    if (measuring() || !loading_.empty() || request.file.empty() || request.file == shown || request.file == wanted)
        return;
    try {
        recent_.remove(request.file);
    } catch (const std::exception& error) {
        problem_ = error.what();
    }
    modelListRequest_ = std::move(request);
}

void ControlPanel::setModelList(std::vector<std::filesystem::path> models,
                                std::vector<std::optional<voxel::VoxelFileSummary>> summaries, std::size_t model)
{
    if (model >= models.size() || summaries.size() != models.size())
        throw std::logic_error("setModelList: the model on screen or a header is missing from the list");
    facts_.models = std::move(models);
    facts_.modelSummaries = std::move(summaries);
    facts_.model = model;
    wantedModel_ = model;
}

void ControlPanel::rememberFile(const std::string& kind, const std::filesystem::path& file)
{
    try {
        recent_.add(kind, file);
    } catch (const std::exception& error) {
        problem_ = error.what(); // the file still opens; only the list of recent files is not saved
    }
}

void ControlPanel::selectEncodingSetting(const std::string& setting, const std::string& value)
{
    if (facts_.encodings.empty() || measuring() || !loading_.empty())
        return;
    wantedEncoding_ = facts_.encodingChoices.pick(wantedEncoding_, setting, value);
    problem_.clear();
}

void ControlPanel::rayOrderChanged(std::uint32_t order)
{
    stopInspecting();
    facts_.rayOrder = order;
    wantedRayOrder_ = order;
    depthVersion_ = 0;
    // The summary names its costliest ray by its index in the old ray list: it goes, and the
    // next one comes with the next counted frame.
    workSummary_.reset();
    workSummaryTime_ = {};
    finishedFrame_.reset(); // its buffers belonged to the old frame pipeline
    // Another order can be faster or slower: the old times belong to the old order.
    traceTimes_.assign(facts_.tracers.size(), {});
    traceMs_ = {};
}

void ControlPanel::selectRayOrder(std::uint32_t order)
{
    if (!measuring() && loading_.empty())
        wantedRayOrder_ = order;
}

void ControlPanel::selectBookmark(std::size_t index)
{
    if (measuring() || index >= bookmarks_.list().size())
        return;
    const CameraBookmark& bookmark = bookmarks_.list()[index];
    stopInspecting();
    camera_ = bookmark.cameraIn(facts_.sceneBounds);
    cameraMoved();
    cameraPreset_ = bookmark.preset();
    bookmark_ = bookmark.name;
}

void ControlPanel::saveBookmark(std::string name)
{
    if (name.empty())
        name = bookmarks_.freeName();
    try {
        const CameraBookmark bookmark = CameraBookmark::of(name, camera_, facts_.sceneBounds);
        bookmarks_.put(bookmark);
        bookmark_ = name;
        // The camera now stands at this bookmark, as after selectBookmark(): an orbit bookmark
        // makes the benchmark command an orbit.
        cameraPreset_ = bookmark.preset();
        tellUser("Camera kept as \"" + name + "\"");
    } catch (const std::exception& error) {
        problem_ = error.what();
    }
}

void ControlPanel::removeBookmark(std::size_t index)
{
    try {
        if (index < bookmarks_.list().size() && bookmark_ == bookmarks_.list()[index].name)
            bookmark_.reset();
        bookmarks_.remove(index);
        // The row being renamed keeps its bookmark: rows below the deleted one move up.
        if (renaming_ && *renaming_ == index)
            renaming_.reset();
        else if (renaming_ && *renaming_ > index)
            --*renaming_;
    } catch (const std::exception& error) {
        problem_ = error.what();
    }
}

void ControlPanel::copyCommand(CommandForm form)
{
    if (!facts_.command)
        return;
    copiedCommand_ = facts_.command(windowState(), form);
    ImGui::SetClipboardText(copiedCommand_.c_str());
}

void ControlPanel::sceneRebuilt(PanelFacts facts)
{
    const auto sameName = [](const PanelFacts::Tracer& a, const PanelFacts::Tracer& b) { return a.name == b.name; };
    if (!std::equal(facts.tracers.begin(), facts.tracers.end(), facts_.tracers.begin(), facts_.tracers.end(),
                    sameName)) {
        setFacts(std::move(facts));
        return;
    }
    facts_ = std::move(facts);
    finishedFrame_.reset(); // its tracer objects are gone; the next frame reports again
}

void ControlPanel::setFacts(PanelFacts facts)
{
    stopInspecting();
    const std::string previous = facts_.tracers.at(activeTracer_).name;
    const std::optional<std::string> previousSplit =
        splitTracer_ ? std::optional(facts_.tracers.at(*splitTracer_).name) : std::nullopt;
    facts_ = std::move(facts);
    if (const std::filesystem::path& shown = facts_.models.at(facts_.model); !shown.empty())
        rememberFile("models", shown);
    structure_ = {};
    depthVersion_ = 0;
    const auto same = std::find_if(facts_.tracers.begin(), facts_.tracers.end(),
                                   [&](const PanelFacts::Tracer& tracer) { return tracer.name == previous; });
    activeTracer_ = same == facts_.tracers.end() ? 0 : static_cast<std::size_t>(same - facts_.tracers.begin());
    // The tracer of the A/B image by name too; off if the new scene has no such tracer.
    splitTracer_.reset();
    if (previousSplit) {
        const auto split =
            std::find_if(facts_.tracers.begin(), facts_.tracers.end(),
                         [&](const PanelFacts::Tracer& tracer) { return tracer.name == *previousSplit; });
        if (split != facts_.tracers.end())
            compareInImage(static_cast<std::size_t>(split - facts_.tracers.begin()));
    }
    wantedModel_ = facts_.model;
    wantedEncoding_ = facts_.encoding;
    wantedBrickSize_ = facts_.brickSize;
    wantedRayOrder_ = facts_.rayOrder;
    if (unavailableView(shading_.mode))
        shading_.mode = ShadingMode::Shaded;

    // The old times belong to the old scene.
    traceTimes_.assign(facts_.tracers.size(), {});
    measurement_.reset();
    frameInFlight_.reset();
    finishedFrame_.reset();
    traceMs_ = {};
    rayLog_.reset();
    rayLogLines_.clear();
    workSummary_.reset();
    workSummaryTime_ = {};
    rememberEncodingSize();
    picking_ = std::exchange(pickAfterReload_, false);
}

void ControlPanel::reportProblem(std::string message)
{
    problem_ = std::move(message);
}

void ControlPanel::rememberEncodingSize()
{
    if (facts_.brickSize != encodingSizesBrickSize_) {
        encodingSizes_.clear(); // the sizes that are known belong to another brick size
        encodingSizesBrickSize_ = facts_.brickSize;
    }
    encodingSizes_.resize(facts_.encodings.size());
    if (facts_.encoding < encodingSizes_.size() && facts_.voxelCount > 0) {
        encodingSizes_[facts_.encoding] = EncodingSize{facts_.sceneBytes, 8.0 * static_cast<double>(facts_.sceneBytes) /
                                                                              static_cast<double>(facts_.voxelCount)};
    }
}

double& ControlPanel::shaderSeconds(std::size_t tracer)
{
    const std::string encoding = facts_.encodings.empty() ? std::string() : facts_.encodings.at(facts_.encoding).name;
    return shaderSeconds_[facts_.tracers.at(tracer).name + "/" + encoding];
}

bool ControlPanel::shaderIsNew(std::size_t tracer)
{
    return shaderSeconds(tracer) < kNewShaderSeconds;
}

void ControlPanel::selectNextTracer(int step)
{
    const auto count = static_cast<int>(facts_.tracers.size());
    if (count > 0 && !measuring()) {
        selectTracer(static_cast<std::size_t>((static_cast<int>(activeTracer_) + count + step) % count));
    }
}

void ControlPanel::selectTracer(std::size_t index)
{
    if (activeTracer_ == index || measuring())
        return;
    const auto pixel = inspectedPixel_;
    const auto observer = camera_;
    const bool keepObserver = observing_;
    const auto previousTracer = activeTracer_;
    const auto previousWork = rayLogWork_;
    const bool complete = rayLog_ && !rayLog_->lostSteps;
    stopInspecting();
    activeTracer_ = index;
    workSummary_.reset();
    workSummaryTime_ = {}; // the new tracer's summary with its first counted frame
    depthVersion_ = 0;     // the depth came from the hits of the previous tracer (none for a baseline)
    if (splitTracer_ == index)
        splitTracer_.reset();
    if (const char* reason = unavailableView(shading_.mode)) {
        tellUser(std::string(resolve::kShadingModeNames.at(static_cast<std::size_t>(shading_.mode))) +
                 " is off: " + reason);
        selectView(ShadingMode::Shaded);
    }
    if (pixel && facts_.tracers.at(index).countsWork) {
        inspectPixel(pixel->first, pixel->second);
        if (keepObserver) {
            // Capture the original ray first. Restore the observer once its GPU log arrives.
            pendingObserver_ = observer;
        }
        if (complete)
            comparison_ = std::pair{previousTracer, previousWork};
    }
}

const char* ControlPanel::unavailableView(ShadingMode mode) const
{
    return unavailableViewFor(mode, activeTracer_);
}

const char* ControlPanel::unavailableViewFor(ShadingMode mode, std::size_t index) const
{
    const auto& tracer = facts_.tracers.at(index);
    return resolve::unavailableView(
        mode, {tracer.countsWork, tracer.countsStructureSteps, tracer.countsCycles, facts_.voxelCount > 0,
               !facts_.encodings.empty() && facts_.encodings.at(facts_.encoding).skipsEmptySpace});
}

void ControlPanel::selectView(ShadingMode mode)
{
    if (measuring())
        return;
    if (const char* reason = unavailableView(mode)) {
        tellUser(std::string(resolve::kShadingModeNames.at(static_cast<std::size_t>(mode))) + ": " + reason);
        return;
    }
    switchScale(mode);
    if (mode == ShadingMode::Distance && shading_.mode != mode && autoRanges_)
        depthVersion_ = 0; // the automatic distance range is set when the depth comes
    shading_.mode = mode;
    if (const std::optional<WorkCounter> counter = counterOf(mode))
        histogramCounter_ = *counter;
}

void ControlPanel::switchScale(ShadingMode mode)
{
    if (mode == shading_.mode)
        return;
    if (resolve::showsCount(shading_.mode))
        scales_[shading_.mode] = shading_.workScale;
    if (resolve::showsCount(mode)) {
        const auto known = scales_.find(mode);
        // A view not shown before starts at its default range (the automatic range follows when
        // it is on), not at the range of the view before: cycles and cells differ a thousandfold.
        if (known != scales_.end())
            shading_.workScale = known->second;
        else
            shading_.workScale.maximum = resolve::defaultScaleMaximum(mode);
    }
}

std::optional<WorkCounter> ControlPanel::counterOf(ShadingMode mode)
{
    switch (mode) {
    case ShadingMode::WorkCells:
        return WorkCounter::Cells;
    case ShadingMode::WorkPrimitives:
        return WorkCounter::PrimitiveTests;
    case ShadingMode::WorkRuns:
        return WorkCounter::Runs;
    case ShadingMode::WorkStructure:
        return WorkCounter::StructureSteps;
    case ShadingMode::WorkMipmap:
        return WorkCounter::MipmapCells;
    case ShadingMode::WorkCycles:
        return WorkCounter::Cycles;
    default:
        return std::nullopt;
    }
}

void ControlPanel::tellUser(std::string message)
{
    notice_ = std::move(message);
    noticeTime_ = std::chrono::steady_clock::now();
}

void ControlPanel::showWarpMaximum(bool on)
{
    shading_.warpSize = on ? facts_.warpSize : 0u;
}

bool ControlPanel::activeTracerWarps() const
{
    return facts_.tracers.at(activeTracer_).warpsFollowRays;
}

void ControlPanel::setAutoRanges(bool on)
{
    autoRanges_ = on;
    if (on)
        depthVersion_ = 0; // the distance range follows at the next frame
}

void ControlPanel::compareInImage(std::optional<std::size_t> index)
{
    if (index && (*index >= facts_.tracers.size() || *index == activeTracer_))
        index.reset();
    splitTracer_ = index;
}

std::optional<std::size_t> ControlPanel::splitTracer() const
{
    if (!splitTracer_ || measuring() || *splitTracer_ >= facts_.tracers.size() ||
        unavailableViewFor(shading_.mode, *splitTracer_))
        return std::nullopt;
    return splitTracer_;
}

bool ControlPanel::imageWarps() const
{
    const std::optional<std::size_t> split = splitTracer();
    return facts_.tracers.at(activeTracer_).warpsFollowRays && (!split || facts_.tracers.at(*split).warpsFollowRays);
}

resolve::ShadingParams ControlPanel::shadingFor(std::size_t tracer) const
{
    resolve::ShadingParams params = shading_;
    // WHY both sides of the A/B image: they share one legend, so they must show the same quantity.
    if (!facts_.tracers.at(tracer).warpsFollowRays || !imageWarps())
        params.warpSize = 0;
    return params;
}

bool ControlPanel::countWorkFor(std::size_t tracer) const
{
    return resolve::showsWork(shading_.mode) && facts_.tracers.at(tracer).countsWork && !measuring();
}

void ControlPanel::saveImage(std::optional<std::filesystem::path> file)
{
    // WHY not while Quick timing runs: its frames run the timed shader, which shows a work view as
    // the shaded image, so the view's legend and command would label another picture.
    if (!facts_.command || measuring())
        return;
    const bool named = file.has_value();
    if (!file) {
        // scene_tracer_view_time.png in the folder of the last image, else in Pictures.
        std::filesystem::path folder = imageFolder_;
        if (folder.empty()) {
            const char* profile = std::getenv("USERPROFILE");
            folder = profile ? std::filesystem::path(profile) / "Pictures" / "thesis_renderer"
                             : std::filesystem::current_path() / "images";
        }
        const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
        std::string view = resolve::kShadingModeNames.at(static_cast<std::size_t>(shading_.mode));
        for (char& c : view)
            c = std::isalnum(static_cast<unsigned char>(c)) ? static_cast<char>(std::tolower(c)) : '-';
        const std::filesystem::path& model = facts_.models.at(facts_.model);
        const std::string scene = model.empty() ? std::string("box-field") : utf8(model.stem());
        file = folder / pathFromUtf8(std::format("{}_{}_{}_{:%Y%m%d-%H%M%S}.png", scene,
                                                 facts_.tracers.at(activeTracer_).name, view, now));
    }
    const auto requestFor = [&](const std::filesystem::path& image) {
        SaveImageRequest request;
        request.file = image;
        if (splitTracer()) {
            request.second = image.parent_path() /
                             pathFromUtf8(utf8(image.stem()) + "_B_" + facts_.tracers.at(*splitTracer_).name + ".png");
        }
        request.command = facts_.command(windowState(), CommandForm::Screenshot);
        return request;
    };
    const auto exists = [](const std::filesystem::path& path) {
        std::error_code error;
        return std::filesystem::exists(path, error);
    };
    const auto anyExists = [&](const SaveImageRequest& request) {
        const std::vector<std::filesystem::path> files = request.files();
        return std::any_of(files.begin(), files.end(), exists);
    };
    SaveImageRequest request = requestFor(*file);
    if (!named) {
        // WHY a number: the name has the time in seconds, so two saves in one second share it.
        const std::string stem = utf8(file->stem());
        for (std::uint32_t number = 2; anyExists(request); ++number)
            request = requestFor(file->parent_path() / pathFromUtf8(std::format("{}_{}.png", stem, number)));
    } else if (!exists(request.file)) {
        // The save dialog asked about the image only. Replacing an image replaces its own command
        // and B image with it; a new image must not write over a file that belongs to something else.
        for (const std::filesystem::path& written : request.files()) {
            if (exists(written)) {
                problem_ = "Save image: " + utf8(written) + " already exists; choose another name.";
                saveRequest_.reset(); // "Save as..." asked for a default request first
                return;
            }
        }
    }
    saveRequest_ = std::move(request);
}

void ControlPanel::imageSaved(const std::filesystem::path& file)
{
    imageFolder_ = file.parent_path();
    tellUser("Saved " + utf8(file.filename()));
}

void ControlPanel::fold(PanelWindow window, bool folded)
{
    folded_.at(static_cast<std::size_t>(window)) = folded;
}

void ControlPanel::toggleImageOnly()
{
    const bool allFolded = std::all_of(folded_.begin(), folded_.end(), [](bool f) { return f; });
    folded_.fill(!allFolded);
}

bool ControlPanel::costliestIsCurrent() const
{
    return workSummary_ && workSummary_->rays() > 0 && workSummaryCameraVersion_ == cameraVersion_;
}

bool ControlPanel::inspectCostliest(WorkCounter counter)
{
    // WHY the camera check: the inspector traces the pixel from the camera of now; with a summary
    // of an older camera it would show another ray than the one whose count the summary names.
    if (!costliestIsCurrent())
        return false;
    const std::uint32_t ray = workSummary_->costliestRay(counter);
    // WHY a search: the ray order is written once, as the pixel -> ray rule of RayOrder.h.
    for (std::uint32_t y = 0; y < facts_.height; ++y) {
        for (std::uint32_t x = 0; x < facts_.width; ++x) {
            if (THESIS_RAY_INDEX(facts_.rayOrder, x, y, facts_.width, facts_.height) == ray) {
                inspectPixel(x, y);
                return true;
            }
        }
    }
    return false;
}

void ControlPanel::selectNextEncoding(int step)
{
    const auto count = static_cast<int>(facts_.encodings.size());
    if (count > 0 && !measuring() && loading_.empty()) {
        wantedEncoding_ = static_cast<std::size_t>((static_cast<int>(wantedEncoding_) + count + step) % count);
        problem_.clear();
    }
}

void ControlPanel::selectNextBrickSize(int step)
{
    const auto count = static_cast<int>(facts_.brickSizes.size());
    if (count == 0 || measuring() || !loading_.empty()) {
        return;
    }
    const auto current = std::find(facts_.brickSizes.begin(), facts_.brickSizes.end(), wantedBrickSize_);
    const int index = current == facts_.brickSizes.end() ? 0 : static_cast<int>(current - facts_.brickSizes.begin());
    wantedBrickSize_ = facts_.brickSizes[static_cast<std::size_t>((index + count + step) % count)];
    problem_.clear();
}

void ControlPanel::setWindowFocused(bool focused)
{
    if (focused == windowFocused_)
        return;
    windowFocused_ = focused;
    if (!focused && measuring()) {
        // Some tracers would be timed in front and some in the background: drop the whole run.
        const std::size_t first = measurement_->tracer;
        stopMeasuring();
        for (std::size_t i = 0; i < first && i < traceTimes_.size(); ++i)
            traceTimes_[i] = {};
        reportProblem("Quick timing stopped: the window went to the background.");
    }
}

void ControlPanel::measureAll()
{
    if (facts_.tracers.empty() || measuring()) {
        return;
    }
    if (!windowFocused_) {
        reportProblem("Quick timing needs the window in front.");
        return;
    }
    const auto measuredCamera = camera_;
    stopInspecting();
    camera_ = measuredCamera;
    measurement_ = Measurement{0, activeTracer_, 0, {}};
    activeTracer_ = 0;
}

void ControlPanel::stopMeasuring()
{
    if (measurement_) {
        activeTracer_ = measurement_->returnTo;
        measurement_.reset();
    }
}

void ControlPanel::inspectPixel(std::uint32_t x, std::uint32_t y)
{
    if (x < facts_.width && y < facts_.height && facts_.tracers.at(activeTracer_).countsWork && !measuring()) {
        picking_ = false;
        fold(PanelWindow::Inspector, false); // the walk of the ray is shown there
        comparison_.reset();
        observing_ = false;
        lesson_.reset();
        lessonStage_ = 0;
        lessonEvent_ = 0;
        captureCamera_ = camera_;
        inspectedPixel_ = {x, y};
        rayLog_.reset();
        rayLogLines_.clear();
    }
}

void ControlPanel::stopInspecting()
{
    if (captureCamera_ && observing_) {
        camera_ = *captureCamera_;
        cameraMoved();
    }
    captureCamera_.reset();
    pendingObserver_.reset();
    comparison_.reset();
    lesson_.reset();
    picking_ = false;
    observing_ = false;
    inspectedPixel_.reset();
    rayLog_.reset();
    rayLogLines_.clear();
}

std::optional<std::uint32_t> ControlPanel::loggedRay() const
{
    if (!inspectedPixel_ || lesson_) {
        return std::nullopt;
    }
    return THESIS_RAY_INDEX(facts_.rayOrder, inspectedPixel_->first, inspectedPixel_->second, facts_.width,
                            facts_.height);
}

bool ControlPanel::wantsDepth() const
{
    const bool autoDistance = autoRanges_ && shading_.mode == ShadingMode::Distance;
    return finishedFrame_ && finishedFrame_->cameraVersion == cameraVersion_ &&
           finishedFrame_->tracer == activeTracer_ && depthVersion_ != cameraVersion_ &&
           (structure_.visible() || lesson_.has_value() || autoDistance) && !measuring();
}

void ControlPanel::recordDepth(DepthMap depth)
{
    depth_ = std::move(depth);
    depthVersion_ = finishedFrame_ ? finishedFrame_->cameraVersion : 0;
    if (autoRanges_ && shading_.mode == ShadingMode::Distance) {
        // The range of the distance view: from the nearest to the farthest visible hit.
        float nearest = std::numeric_limits<float>::infinity();
        float farthest = 0.0f;
        for (const float distance : depth_.distance) {
            if (std::isfinite(distance)) {
                nearest = std::min(nearest, distance);
                farthest = std::max(farthest, distance);
            }
        }
        if (std::isfinite(nearest)) {
            shading_.distanceNear = nearest;
            shading_.distanceFar = std::max(farthest, nearest * 1.001f + 1e-3f);
        }
    }
}

void ControlPanel::recordRayLog(std::optional<tracing::RayLog> log)
{
    if (!inspectedPixel_ || !log || log->ray != loggedRay()) {
        return; // keep what is shown until a log of this ray comes
    }
    rayLog_ = std::move(log);
    const glm::uvec2 pixel{inspectedPixel_->first, inspectedPixel_->second};
    const glm::uvec2 resolution{facts_.width, facts_.height};
    lesson_ = makeRayLesson(*rayLog_, facts_.geometry, *captureCamera_, pixel, resolution,
                            structure_.searchStep(*rayLog_, activeTracer_, facts_.tracers.at(activeTracer_).diagram,
                                                  facts_.geometry, facts_.sceneBounds,
                                                  primaryRay(*captureCamera_, pixel, resolution)));
    orientCloseUp();
    if (pendingObserver_) {
        camera_ = *pendingObserver_;
        pendingObserver_.reset();
        observing_ = true;
        cameraMoved();
    }
    rayLogLines_ = describeRayLog(*rayLog_, facts_.tracers.at(activeTracer_).describeStep);
    rayLogWork_ = tracing::countWork(*rayLog_);
}

bool ControlPanel::wantsWorkSummary() const
{
    // Only a frame of the active tracer that ran the counting shader left its work in the buffer;
    // after any other frame (e.g. the last of Quick timing) the buffer holds older work.
    return resolve::showsWork(shading_.mode) && !measuring() && finishedFrame_ && finishedFrame_->countedWork &&
           finishedFrame_->tracer == activeTracer_ &&
           std::chrono::steady_clock::now() - workSummaryTime_ > std::chrono::seconds(1);
}

void ControlPanel::recordWorkSummary(const RayWorkSummary& summary)
{
    workSummary_ = summary;
    workSummaryTime_ = std::chrono::steady_clock::now();
    workSummaryCameraVersion_ = finishedFrame_ ? finishedFrame_->cameraVersion : 0;
    if (const std::optional<WorkCounter> counter = counterOf(shading_.mode); counter && autoRanges_) {
        // The end of the colour map at the 99th percentile of the rays with work: the few rays
        // above it show white, the rest spread over the map.
        const double top = summary.percentile(*counter, 0.99);
        if (top > 0.0)
            shading_.workScale.maximum = static_cast<float>(std::max(1.0, std::ceil(top)));
    }
}

void ControlPanel::fly(bool cameraFree)
{
    const ImGuiIO& io = ImGui::GetIO();
    if (io.MouseWheel != 0.0f) {
        flySpeed_ = std::clamp(flySpeed_ * std::pow(1.25f, io.MouseWheel), 1.0f / 64.0f, 64.0f);
    }
    if (!cameraFree) {
        return;
    }
    bool moved = false;
    // The view turns only once the mouse moved past the drag threshold, so a click stays a click.
    if (rightButtonFlew_ && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)) {
        camera_.turnInPlace(-0.15f * io.MouseDelta.x, 0.15f * io.MouseDelta.y);
        moved = true;
    }
    const auto axis = [](ImGuiKey positive, ImGuiKey negative) {
        return (ImGui::IsKeyDown(positive) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(negative) ? 1.0f : 0.0f);
    };
    const float forward = axis(ImGuiKey_W, ImGuiKey_S);
    const float right = axis(ImGuiKey_D, ImGuiKey_A);
    const float up = axis(ImGuiKey_E, ImGuiKey_Q);
    if (forward != 0.0f || right != 0.0f || up != 0.0f) {
        // 0.35 scene radii per second at speed 1, four times that with Shift.
        const float unitsPerSecond =
            0.35f * std::max(facts_.sceneBounds.radius(), 1e-3f) * flySpeed_ * (io.KeyShift ? 4.0f : 1.0f);
        const float stepLength = unitsPerSecond * io.DeltaTime;
        camera_.fly(forward * stepLength, right * stepLength, up * stepLength);
        rightButtonFlew_ = true;
        moved = true;
    }
    if (moved) {
        observing_ = lesson_.has_value();
        cameraMoved();
    }
}

void ControlPanel::handleInput()
{
    const ImGuiIO& io = ImGui::GetIO();
    if (measuring()) {
        // The camera and the choices stay as they are until every tracer was measured.
        if (!io.WantCaptureKeyboard &&
            (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_M, false))) {
            stopMeasuring();
        }
        return;
    }

    // Holding the right button flies the camera; a right click without moving picks a pixel.
    const bool flying = !io.WantCaptureMouse && ImGui::IsMouseDown(ImGuiMouseButton_Right);
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        rightButtonFlew_ = false;
    }
    // The line of the A/B comparison: a left drag that starts on it moves it.
    if (splitTracer() && facts_.width > 0) {
        const FrameInWindow frame(facts_.width, facts_.height, imageMin_, imageMax_);
        const float lineX = frame.left + splitAt_ * static_cast<float>(facts_.width) * frame.scale;
        if (!io.WantCaptureMouse && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            std::abs(io.MousePos.x - lineX) <= 6.0f && io.MousePos.y >= frame.top &&
            io.MousePos.y <= frame.top + static_cast<float>(facts_.height) * frame.scale)
            draggingSplit_ = true;
        if (draggingSplit_ && ImGui::IsMouseDown(ImGuiMouseButton_Left))
            splitAt_ =
                std::clamp((io.MousePos.x - frame.left) / (static_cast<float>(facts_.width) * frame.scale), 0.0f, 1.0f);
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        draggingSplit_ = false;
    if (!io.WantCaptureMouse && !draggingSplit_) {
        const bool cameraFree = !inspectedPixel_ || lesson_.has_value();
        const bool leftDragged = !picking_ && !flying && ImGui::IsMouseDragging(ImGuiMouseButton_Left) &&
                                 (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f);
        if (leftDragged && io.KeyCtrl) {
            // Ctrl + left-drag turns the light instead of the camera.
            moveLight(-0.01f * io.MouseDelta.x, -0.01f * io.MouseDelta.y);
        } else if (leftDragged && cameraFree) {
            observing_ = lesson_.has_value();
            camera_.yawDegrees -= 0.25f * io.MouseDelta.x;
            camera_.pitchDegrees = std::clamp(camera_.pitchDegrees + 0.25f * io.MouseDelta.y, -89.0f, 89.0f);
            cameraMoved();
        }
        if (flying) {
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
                rightButtonFlew_ = true;
            }
            fly(cameraFree);
        } else if (cameraFree && io.MouseWheel != 0.0f) {
            observing_ = lesson_.has_value();
            camera_.distance = std::max(0.1f, camera_.distance * (1.0f - 0.1f * io.MouseWheel));
            cameraMoved();
        }
        const bool rightClick = ImGui::IsMouseReleased(ImGuiMouseButton_Right) && !rightButtonFlew_;
        if ((rightClick || (picking_ && ImGui::IsMouseClicked(ImGuiMouseButton_Left))) && facts_.width > 0) {
            const FrameInWindow frame(facts_.width, facts_.height, imageMin_, imageMax_);
            const float x = (io.MousePos.x - frame.left) / frame.scale;
            const float y = (io.MousePos.y - frame.top) / frame.scale;
            if (x >= 0.0f && y >= 0.0f) {
                inspectPixel(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
            }
        }
    }
    // While flying, the letter keys steer the camera, so the shortcuts wait.
    if (io.WantCaptureKeyboard || flying) {
        return;
    }

    const int step = io.KeyShift ? -1 : 1;
    if (ImGui::IsKeyPressed(ImGuiKey_Tab, false)) {
        selectNextTracer(step);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_E, false)) {
        selectNextEncoding(step);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_B, false)) {
        selectNextBrickSize(step);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_M, false)) {
        measureAll();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_R, false)) {
        returnCamera();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        stopInspecting();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_V, false) && lesson_) {
        toggleSideView();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        structure_.show(!structure_.visible());
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F, false)) {
        toggleImageOnly();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_P, false)) {
        saveImage();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
        nextRayStep();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
        previousRayStep();
    }
    for (const std::span<const ViewInfo> views : {imageViews(), workViews()}) {
        for (const ViewInfo& view : views) {
            if (ImGui::IsKeyPressed(view.key, false)) {
                selectView(view.mode);
            }
        }
    }
}

void ControlPanel::recordTimings(const std::vector<gpu::StageTiming>& timings)
{
    const bool counted = frameInFlight_ && frameInFlight_->countedWork;
    if (lastFrameCounted_ != counted)
        traceMs_ = {};
    lastFrameCounted_ = counted;
    double trace = 0.0;
    for (const gpu::StageTiming& timing : timings) {
        const std::string_view stage = timing.stage;
        if (stage == kStageRayGeneration) {
            rayGenerationMs_.add(timing.milliseconds);
        } else if (stage == kStageTrace) {
            trace = timing.milliseconds;
            traceMs_.add(timing.milliseconds);
        } else if (stage == kStageShading) {
            shadingMs_.add(timing.milliseconds);
        }
    }

    finishedFrame_ = std::exchange(frameInFlight_, std::nullopt);
    if (!finishedFrame_) {
        return;
    }
    const FrameInFlight frame = *finishedFrame_;
    // A frame that counted work ran the counting shader. Its time is not the time of the tracer.
    // Nor is the time of a frame that ran a second tracer for the A/B comparison.
    if (frame.countedWork || frame.split || frame.tracer >= traceTimes_.size() || trace <= 0.0) {
        return;
    }

    if (!windowFocused_) {
        return; // a frame of a background window: not comparable with the others
    }
    if (!measurement_) {
        // Live: the average of the last frames while the camera stands still.
        TraceTime& time = traceTimes_[frame.tracer];
        const bool sameCamera = time.cameraVersion == frame.cameraVersion;
        time.milliseconds = sameCamera ? 0.9 * time.milliseconds + 0.1 * trace : trace;
        time.cameraVersion = frame.cameraVersion;
        time.fromNewShader = shaderIsNew(frame.tracer);
        return;
    }

    Measurement& measurement = *measurement_;
    if (frame.tracer != measurement.tracer || shaderIsNew(frame.tracer)) {
        return; // a new shader runs first until the driver has made it fast
    }
    if (measurement.skipped < kMeasureSkippedFrames) {
        ++measurement.skipped;
        return;
    }
    measurement.samples.push_back(trace);
    if (measurement.samples.size() < kMeasureFrames) {
        return;
    }
    traceTimes_[measurement.tracer] = {core::median(measurement.samples), cameraVersion_, false};
    if (measurement.tracer + 1 == facts_.tracers.size()) {
        stopMeasuring();
        return;
    }
    measurement.tracer += 1;
    measurement.skipped = 0;
    measurement.samples.clear();
    activeTracer_ = measurement.tracer;
}

std::optional<double> ControlPanel::floorMilliseconds() const
{
    // The time of a baseline tracer. It reads every ray and writes a miss, so it does not
    // depend on the camera: a time from any camera position will do.
    std::optional<double> floor;
    for (std::size_t i = 0; i < facts_.tracers.size(); ++i) {
        if (!facts_.tracers[i].findsHits && traceTimes_[i].cameraVersion != 0 && !traceTimes_[i].fromNewShader) {
            floor = floor ? std::min(*floor, traceTimes_[i].milliseconds) : traceTimes_[i].milliseconds;
        }
    }
    return floor;
}

void ControlPanel::drawDockLayout()
{
    // A host window over the viewport, above the status bar, that holds the dock space. Its
    // central node stays empty and lets the mouse through: the image is drawn there, behind ImGui.
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float statusHeight = ImGui::GetFrameHeight();
    const ImVec2 size{viewport->WorkSize.x, std::max(1.0f, viewport->WorkSize.y - statusHeight)};
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(size);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    constexpr ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoDecoration |
                                           ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                           ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground |
                                           ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("##dock host", nullptr, hostFlags);
    ImGui::PopStyleVar(2);

    const ImGuiID dock = ImGui::GetID("layout");
    if (!layoutBuilt_ || layoutFolded_ != folded_) {
        // Fixed areas: settings left, inspector right, measurements under the image. A folded
        // window gets no area: the image takes its room. Built again when a window folds or opens.
        layoutBuilt_ = true;
        layoutFolded_ = folded_;
        ImGui::DockBuilderRemoveNode(dock);
        ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dock, size);
        ImGuiID centre = dock;
        if (!folded(PanelWindow::Settings)) {
            const ImGuiID left = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Left, 0.21f, nullptr, &centre);
            ImGui::DockBuilderDockWindow(kSettingsWindow, left);
        }
        if (!folded(PanelWindow::Inspector)) {
            const float share = folded(PanelWindow::Settings) ? 0.23f : 0.28f; // the same width either way
            const ImGuiID right = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, share, nullptr, &centre);
            ImGui::DockBuilderDockWindow(kInspectorWindow, right);
        }
        if (!folded(PanelWindow::Measurements)) {
            const ImGuiID bottom = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down, 0.27f, nullptr, &centre);
            ImGui::DockBuilderDockWindow(kMeasurementsWindow, bottom);
        }
        ImGui::DockBuilderFinish(dock);
    }
    ImGui::DockSpace(dock, {0.0f, 0.0f}, ImGuiDockNodeFlags_PassthruCentralNode);
    if (const ImGuiDockNode* centre = ImGui::DockBuilderGetCentralNode(dock)) {
        imageMin_ = centre->Pos;
        imageMax_ = {centre->Pos.x + centre->Size.x, centre->Pos.y + centre->Size.y};
    }
    ImGui::End();
}

void ControlPanel::draw()
{
    drawDockLayout();
    // Each window has a close button in its tab: it folds the window (the toolbar opens it again).
    const auto window = [this](PanelWindow which, const char* name, void (ControlPanel::*content)()) {
        if (folded(which))
            return;
        bool open = true;
        if (ImGui::Begin(name, &open))
            (this->*content)();
        ImGui::End();
        if (!open)
            fold(which, true);
    };
    window(PanelWindow::Settings, kSettingsWindow, &ControlPanel::drawSettings);
    window(PanelWindow::Inspector, kInspectorWindow, &ControlPanel::drawInspector);
    window(PanelWindow::Measurements, kMeasurementsWindow, &ControlPanel::drawMeasurements);
    drawStatusBar();
    drawImageOverlay();
    drawImageToolbar();

    // The choices of this frame are final now. recordTimings() gets its times in the next frame.
    frameInFlight_ =
        FrameInFlight{activeTracer_, cameraVersion_, countWork() && facts_.tracers.at(activeTracer_).countsWork,
                      splitTracer().has_value()};
    if (!frameInFlight_->countedWork) {
        shaderSeconds(activeTracer_) += static_cast<double>(ImGui::GetIO().DeltaTime);
    }
}

void ControlPanel::drawImageOverlay() const
{
    const FrameInWindow frame(facts_.width, facts_.height, imageMin_, imageMax_);
    const float width = static_cast<float>(facts_.width), height = static_cast<float>(facts_.height);
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    // Nothing of the overlay may spill over the windows around the image.
    draw->PushClipRect(imageMin_, imageMax_, true);

    // The legend of the view, on the image in its bottom left corner.
    // shading(), not shading_: the image of a tracer without warps in list order shows each ray.
    const float margin = ImGui::GetFontSize();
    const float legendWidth = std::min(imageMax_.x - imageMin_.x - 2.0f * margin, 32.0f * ImGui::GetFontSize());
    if (const resolve::ViewLegend legend = resolve::legendFor(shading()); !legend.empty() && legendWidth > margin) {
        const float legendTall = legendHeight(legend, legendWidth);
        const ImVec2 origin{imageMin_.x + margin, imageMax_.y - margin - legendTall};
        draw->AddRectFilled({origin.x - 8.0f, origin.y - 6.0f},
                            {origin.x + legendWidth + 8.0f, imageMax_.y - margin + 6.0f}, IM_COL32(18, 18, 18, 210),
                            4.0f);
        drawLegend(draw, origin, legendWidth, legend);
    }

    // The depth map belongs to one camera position. While the camera moves, lines are drawn plain.
    const OverlayPainter painter(draw, camera_.frame(width / height), frame.left, frame.top, width * frame.scale,
                                 height * frame.scale, depthVersion_ == cameraVersion_ ? &depth_ : nullptr);
    if (facts_.tracers.at(activeTracer_).findsHits)
        structure_.draw(painter);
    if (inspectedPixel_ && lesson_)
        drawRayLesson(*lesson_, lessonStage_, lessonEvent_, painter);
    if (inspectedPixel_ && !(lesson_ && observing_)) {
        const ImVec2 centre{frame.left + (static_cast<float>(inspectedPixel_->first) + 0.5f) * frame.scale,
                            frame.top + (static_cast<float>(inspectedPixel_->second) + 0.5f) * frame.scale};
        draw->AddCircle(centre, 9.0f, IM_COL32(0, 0, 0, 255), 0, 4.0f);
        draw->AddCircle(centre, 9.0f, IM_COL32(255, 230, 60, 255), 0, 2.0f);
    }
    draw->PopClipRect();
    drawSplitOverlay();
}

void ControlPanel::drawSplitOverlay() const
{
    const std::optional<std::size_t> right = splitTracer();
    if (!right)
        return;
    const FrameInWindow frame(facts_.width, facts_.height, imageMin_, imageMax_);
    const float top = frame.top;
    const float bottom = frame.top + static_cast<float>(facts_.height) * frame.scale;
    const float x = frame.left + splitAt_ * static_cast<float>(facts_.width) * frame.scale;
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    draw->PushClipRect(imageMin_, imageMax_, true);
    draw->AddLine({x, top}, {x, bottom}, IM_COL32(0, 0, 0, 200), 4.0f);
    draw->AddLine({x, top}, {x, bottom}, IM_COL32(255, 255, 255, 255), 2.0f);
    const float middle = 0.5f * (top + bottom);
    draw->AddCircleFilled({x, middle}, 7.0f, IM_COL32(255, 255, 255, 255));
    draw->AddCircle({x, middle}, 7.0f, IM_COL32(0, 0, 0, 255), 0, 2.0f);
    // The names of both sides, at the top of each side of the line.
    const auto label = [&](const std::string& text, float at, bool leftSide) {
        const ImVec2 size = ImGui::CalcTextSize(text.c_str());
        const float pad = 4.0f;
        const float left = leftSide ? at - size.x - 3.0f * pad : at + pad;
        const ImVec2 corner{left, top + pad};
        draw->AddRectFilled(corner, {corner.x + size.x + 2.0f * pad, corner.y + size.y + 2.0f * pad},
                            IM_COL32(18, 18, 18, 210), 3.0f);
        draw->AddText({corner.x + pad, corner.y + pad}, IM_COL32(255, 255, 255, 255), text.c_str());
    };
    label(std::string("A  ") + tracerLabel(facts_.tracers.at(activeTracer_).name), x, true);
    label(std::string("B  ") + tracerLabel(facts_.tracers.at(*right).name), x, false);
    draw->PopClipRect();
}

void ControlPanel::drawImageToolbar()
{
    // A small bar in the top left corner of the image: open or fold the windows, save the image.
    const float margin = 6.0f;
    ImGui::SetNextWindowPos({imageMin_.x + margin, imageMin_.y + margin});
    ImGui::SetNextWindowBgAlpha(0.75f);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                       ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                                       ImGuiWindowFlags_NoMove;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {4.0f, 4.0f});
    if (ImGui::Begin("##image toolbar", nullptr, flags)) {
        const auto toggle = [this](PanelWindow which, const char* label, const char* tip) {
            const bool open = !folded(which);
            if (open)
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::SmallButton(label))
                fold(which, open);
            if (open)
                ImGui::PopStyleColor();
            ImGui::SetItemTooltip("%s", tip);
            ImGui::SameLine();
        };
        toggle(PanelWindow::Settings, "Settings", "Open or fold the settings (left).");
        toggle(PanelWindow::Inspector, "Inspector", "Open or fold the inspector (right).");
        toggle(PanelWindow::Measurements, "Measurements", "Open or fold the measurements (below).");
        if (ImGui::SmallButton("Image only (F)"))
            toggleImageOnly();
        ImGui::SetItemTooltip("Folds every window; F again opens them all.");
        ImGui::SameLine();
        // Quick timing shows the shaded image whatever the view: nothing to save under the view's name.
        ImGui::BeginDisabled(measuring());
        if (ImGui::SmallButton("Save (P)"))
            saveImage();
        ImGui::SetItemTooltip("Writes the frame as a PNG with its legend, and the command that renders it again "
                              "(.txt), into the folder of the last image. With A/B on, also the B image.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Save as...")) {
            try {
                saveImage();
                if (saveRequest_) {
                    if (auto file = pickSaveFile({{"PNG image", {"png"}}}, saveRequest_->file)) {
                        // Append, not replace: "lucy.v1" stays "lucy.v1.png". ".PNG" counts too.
                        std::string extension = utf8(file->extension());
                        for (char& c : extension)
                            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                        if (extension != ".png")
                            *file += ".png";
                        saveImage(*file);
                    } else {
                        saveRequest_.reset();
                    }
                }
            } catch (const std::exception& error) {
                saveRequest_.reset();
                problem_ = error.what();
            }
        }
        ImGui::EndDisabled();
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void ControlPanel::nextRayStep()
{
    if (!lesson_)
        return;
    if (lessonEvent_ + 1 < lesson_->stages.at(lessonStage_).events.size()) {
        ++lessonEvent_;
    } else if (lessonStage_ + 1 < lesson_->stages.size()) {
        ++lessonStage_;
        lessonEvent_ = 0;
        orientCloseUp();
    }
}

void ControlPanel::orientCloseUp()
{
    if (!lesson_ || !captureCamera_)
        return;
    const RayStage& stage = lesson_->stages.at(lessonStage_);
    if (stage.cumulative && stage.closeUp) {
        // Columns: look along the columns, tilted by 30 degrees, so the grid of columns reads as cells.
        const int axis = lesson_->heightAxis;
        closeUp_.yaw = axis == 0 ? 90.0f : axis == 2 ? 0.0f : captureCamera_->yawDegrees;
        closeUp_.pitch = axis == 1 ? 60.0f : 30.0f;
    } else {
        // Brick and hit: turned against the camera, so the ray crosses the brick sideways.
        closeUp_.yaw = captureCamera_->yawDegrees + 70.0f;
        closeUp_.pitch = 20.0f;
    }
    closeUp_.zoom = 1.0f;
    closeUp_.pan = glm::vec2(0.0f);
}

void ControlPanel::previousRayStep()
{
    if (!lesson_)
        return;
    if (lessonEvent_ > 0) {
        --lessonEvent_;
    } else if (lessonStage_ > 0) {
        --lessonStage_;
        lessonEvent_ = lesson_->stages.at(lessonStage_).events.size() - 1;
        orientCloseUp();
    }
}

void ControlPanel::toggleSideView()
{
    if (observing_ && captureCamera_) {
        camera_ = *captureCamera_;
        observing_ = false;
        cameraMoved();
    } else {
        showRayFromSide();
    }
}

void ControlPanel::showRayFromSide()
{
    if (!lesson_)
        return;
    camera_ = *captureCamera_;
    camera_.yawDegrees += 72.0f;
    camera_.target = lesson_->origin + lesson_->direction * (lesson_->length * 0.5f);
    camera_.distance = std::max(0.1f, lesson_->length * 1.45f);
    observing_ = true;
    cameraMoved();
}

void ControlPanel::showBrick()
{
    if (!lesson_ || !lesson_->brickCell || !captureCamera_)
        return;
    camera_ = *captureCamera_;
    camera_.target = (lesson_->brickCell->lo + lesson_->brickCell->hi) * 0.5f;
    camera_.distance = std::max(0.1f, 4.0f * glm::length(lesson_->brickCell->hi - lesson_->brickCell->lo));
    observing_ = true;
    cameraMoved();
}

const char* ControlPanel::tracerLabel(const std::string& name) const
{
    for (const auto& tracer : facts_.tracers)
        if (tracer.name == name)
            return tracer.label.empty() ? tracer.name.c_str() : tracer.label.c_str();
    return name.c_str();
}

bool ControlPanel::encodingSkips(const std::string& name) const
{
    return std::any_of(facts_.encodings.begin(), facts_.encodings.end(), [&](const PanelFacts::Encoding& encoding) {
        return encoding.name == name && encoding.skipsEmptySpace;
    });
}

std::optional<std::string> ControlPanel::takeComparisonRequest()
{
    return std::exchange(compareRequest_, std::nullopt);
}

void ControlPanel::showComparison(ViewComparison result)
{
    result_ = std::move(result);
    resultBrickSize_ = facts_.brickSize;
}

void ControlPanel::moveLight(float azimuthRadians, float elevationRadians)
{
    constexpr float kTwoPi = 6.28318531f;
    constexpr float kMaxElevation = 1.5707963f; // 90 deg: straight above or below
    resolve::LightDirection& light = shading_.light;
    light.azimuth = std::remainder(light.azimuth + azimuthRadians, kTwoPi);
    light.elevation = std::clamp(light.elevation + elevationRadians, -kMaxElevation, kMaxElevation);
}

} // namespace thesis::app
