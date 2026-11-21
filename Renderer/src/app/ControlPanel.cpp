// The state of the panel, the input on the image, the timing of the frames, and the dock layout.
// The windows are drawn in ControlPanelSettings.cpp, ControlPanelInspector.cpp,
// ControlPanelMeasurements.cpp and ControlPanelStatusBar.cpp.

#include "ControlPanel.h"

#include "FramePipeline.h"
#include "PanelWidgets.h"

#include "thesis/core/Statistics.h"
#include "thesis/shared/RayLog.h"
#include "thesis/shared/RayOrder.h"

#include <glm/geometric.hpp>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
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
    state.shading = shading_;
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
    // Another order can be faster or slower: the old times belong to the old order.
    traceTimes_.assign(facts_.tracers.size(), {});
    traceMs_ = {};
}

void ControlPanel::selectRayOrder(std::uint32_t order)
{
    if (!measuring() && loading_.empty())
        wantedRayOrder_ = order;
}

void ControlPanel::selectCameraPreset(std::size_t index)
{
    if (measuring())
        return;
    const CameraPreset& preset = kCameraPresets.at(index);
    stopInspecting();
    camera_ = core::OrbitCamera::framing(facts_.sceneBounds);
    camera_.yawDegrees = preset.camera[0];
    camera_.pitchDegrees = preset.camera[1];
    camera_.distance *= preset.camera[2];
    cameraMoved();
    cameraPreset_ = preset.camera;
}

void ControlPanel::copyCommand(CommandForm form)
{
    if (!facts_.command)
        return;
    copiedCommand_ = facts_.command(windowState(), form);
    ImGui::SetClipboardText(copiedCommand_.c_str());
}

void ControlPanel::setFacts(PanelFacts facts)
{
    stopInspecting();
    const std::string previous = facts_.tracers.at(activeTracer_).name;
    facts_ = std::move(facts);
    structure_ = {};
    depthVersion_ = 0;
    const auto same = std::find_if(facts_.tracers.begin(), facts_.tracers.end(),
                                   [&](const PanelFacts::Tracer& tracer) { return tracer.name == previous; });
    activeTracer_ = same == facts_.tracers.end() ? 0 : static_cast<std::size_t>(same - facts_.tracers.begin());
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
    traceMs_ = {};
    rayLog_.reset();
    rayLogLines_.clear();
    workSummary_.reset();
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
    if (unavailableView(shading_.mode))
        shading_.mode = ShadingMode::Shaded;
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
    const auto& tracer = facts_.tracers.at(activeTracer_);
    if (resolve::showsWork(mode) && !tracer.countsWork)
        return "This tracer does not count work.";
    if (mode == ShadingMode::WorkStructure && !tracer.countsStructureSteps)
        return "This tracer does not report the steps of its search structure.";
    if (!facts_.voxelCount &&
        (mode == ShadingMode::Normal || mode == ShadingMode::Faces || mode == ShadingMode::WorkCells ||
         mode == ShadingMode::WorkRuns || mode == ShadingMode::WorkHitCell))
        return "Needs a voxel model.";
    if ((mode == ShadingMode::WorkMipmap || mode == ShadingMode::WorkSkipLevel) &&
        (facts_.encodings.empty() || !facts_.encodings.at(facts_.encoding).skipsEmptySpace))
        return "Needs an encoding with a mipmap.";
    return nullptr;
}

void ControlPanel::selectView(ShadingMode mode)
{
    if (!unavailableView(mode) && !measuring())
        shading_.mode = mode;
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
    return frameInFlight_ && frameInFlight_->cameraVersion == cameraVersion_ && depthVersion_ != cameraVersion_ &&
           (structure_.visible() || lesson_.has_value()) && !measuring();
}

void ControlPanel::recordDepth(DepthMap depth)
{
    depth_ = std::move(depth);
    depthVersion_ = frameInFlight_ ? frameInFlight_->cameraVersion : 0;
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
    return resolve::showsWork(shading_.mode) && !measuring() && facts_.tracers.at(activeTracer_).countsWork &&
           std::chrono::steady_clock::now() - workSummaryTime_ > std::chrono::seconds(1);
}

void ControlPanel::recordWorkSummary(const RayWorkSummary& summary)
{
    workSummary_ = summary;
    workSummaryTime_ = std::chrono::steady_clock::now();
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
    if (!io.WantCaptureMouse) {
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

    if (!frameInFlight_) {
        return;
    }
    const FrameInFlight frame = *frameInFlight_;
    frameInFlight_.reset();
    // A frame that counted work ran the counting shader. Its time is not the time of the tracer.
    if (frame.countedWork || frame.tracer >= traceTimes_.size() || trace <= 0.0) {
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
    if (!layoutBuilt_) {
        // Fixed areas: settings left, inspector right, measurements under the image.
        layoutBuilt_ = true;
        ImGui::DockBuilderRemoveNode(dock);
        ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dock, size);
        ImGuiID centre = dock;
        const ImGuiID left = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Left, 0.21f, nullptr, &centre);
        const ImGuiID right = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, 0.28f, nullptr, &centre);
        const ImGuiID bottom = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down, 0.27f, nullptr, &centre);
        ImGui::DockBuilderDockWindow(kSettingsWindow, left);
        ImGui::DockBuilderDockWindow(kInspectorWindow, right);
        ImGui::DockBuilderDockWindow(kMeasurementsWindow, bottom);
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
    if (ImGui::Begin(kSettingsWindow)) {
        drawSettings();
    }
    ImGui::End();
    if (ImGui::Begin(kInspectorWindow)) {
        drawInspector();
    }
    ImGui::End();
    if (ImGui::Begin(kMeasurementsWindow)) {
        drawMeasurements();
    }
    ImGui::End();
    drawStatusBar();
    drawImageOverlay();

    // The choices of this frame are final now. recordTimings() gets its times in the next frame.
    frameInFlight_ =
        FrameInFlight{activeTracer_, cameraVersion_, countWork() && facts_.tracers.at(activeTracer_).countsWork};
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
    if (const resolve::ViewLegend legend = resolve::legendFor(shading_); !legend.empty()) {
        const float margin = ImGui::GetFontSize();
        const float legendWidth = std::min(imageMax_.x - imageMin_.x - 2.0f * margin, 32.0f * ImGui::GetFontSize());
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
