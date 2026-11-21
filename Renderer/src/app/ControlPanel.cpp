#include "ControlPanel.h"

#include "FramePipeline.h"
#include "Presenter.h"

#include "thesis/core/Statistics.h"
#include "thesis/shared/RayLog.h"
#include "thesis/shared/RayOrder.h"

#include <glm/geometric.hpp>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <span>
#include <string_view>
#include <utility>

namespace thesis::app {
namespace {

// "Quick timing": the first frames of each tracer are not used, the frames after them are.
constexpr std::uint32_t kMeasureSkippedFrames = 10;
constexpr std::size_t kMeasureFrames = 60;

// The driver runs a shader slower for about 5 seconds after its first use (Benchmark.cpp has
// the details). A shader that ran for less than this time is "new": its time is too high.
constexpr double kNewShaderSeconds = 7.0;

// One view of the image: its key, its name in the panel, and what its colours mean.
struct ViewInfo {
    resolve::ShadingMode mode;
    ImGuiKey key;
    const char* keyName;
    const char* name;
    const char* legend;
};

using resolve::ShadingMode;

constexpr ViewInfo kImageViews[] = {
    {ShadingMode::Shaded, ImGuiKey_1, "1", "Shaded", "Light on the stored normal of the voxel that was hit."},
    {ShadingMode::Distance, ImGuiKey_2, "2", "Distance", "Distance of the hit. Near is white, far is black."},
    {ShadingMode::PrimitiveId, ImGuiKey_3, "3", "Primitive id",
     "One colour for each primitive. For a voxel model: one colour for each brick."},
    {ShadingMode::Normal, ImGuiKey_4, "4", "Stored normal", "The stored normal as a colour: x red, y green, z blue."},
    {ShadingMode::Faces, ImGuiKey_5, "5", "Voxel faces",
     "Light on the face of the voxel cube that was hit. It shows the single voxels."},
};

constexpr ViewInfo kWorkViews[] = {
    {ShadingMode::WorkCells, ImGuiKey_6, "6", "Cells visited", "Cells of primary grids that the ray visited."},
    {ShadingMode::WorkPrimitives, ImGuiKey_7, "7", "Primitives tested",
     "Primitives that the ray was tested against. For a voxel model: bricks."},
    {ShadingMode::WorkRuns, ImGuiKey_8, "8", "Runs tested", "Runs that the ray was tested against."},
    {ShadingMode::WorkHitCell, ImGuiKey_9, "9", "Cell kind at the hit",
     "Kind of the cell with the hit. Green: one voxel. Blue: one run. Red: several runs."},
    {ShadingMode::WorkStructure, ImGuiKey_0, "0", "Search structure steps",
     "Cells or nodes of the tracer's own search structure that the ray read. The hardware tracer cannot count them."},
    {ShadingMode::WorkMipmap, ImGuiKey_Minus, "-", "Mipmap cells read",
     "Cells of the min/max mipmaps that the ray read. Zero for an encoding without mipmap."},
    {ShadingMode::WorkSkipLevel, ImGuiKey_Equal, "=", "Largest mipmap square skipped",
     "The largest square of the min/max mipmap that the walk skipped for this ray, because the ray passes above or "
     "below every voxel in it. Blue: 2x2 cells, green: 4x4, yellow: 8x8, orange: 16x16, red: 32x32. Black: no "
     "square skipped, or the encoding has no mipmap."},
};

constexpr std::span<const ViewInfo> kViewGroups[] = {kImageViews, kWorkViews};

static_assert(std::size(kImageViews) + std::size(kWorkViews) == resolve::kShadingModeNames.size());

std::string formatBytes(std::uint64_t bytes)
{
    char buffer[32];
    if (bytes >= (1ull << 20)) {
        std::snprintf(buffer, sizeof(buffer), "%.1f MiB", static_cast<double>(bytes) / (1 << 20));
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.1f KiB", static_cast<double>(bytes) / (1 << 10));
    }
    return buffer;
}

// Text that breaks at the right side of the panel.
void wrapped(const std::string& text, bool disabled = false)
{
    ImGui::PushTextWrapPos(0.0f);
    if (disabled) {
        ImGui::TextDisabled("%s", text.c_str());
    } else {
        ImGui::TextUnformatted(text.c_str());
    }
    ImGui::PopTextWrapPos();
}

ImU32 imguiColor(resolve::DisplayColor color)
{
    return ImGui::ColorConvertFloat4ToU32({color.r, color.g, color.b, 1.0f});
}

// The colour bar of a count view: a black box for 0, the colour map with ticks, a white box for
// counts above the maximum. The colours are the ones of the image (resolve::ColorScale), the same
// bar that --screenshot puts below the image. Hovering the map shows the count at that colour.
void drawColorBar(const resolve::ColorScale& scale, const char* title)
{
    ImGui::TextUnformatted(title);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float fontSize = ImGui::GetFontSize();
    const float width = ImGui::GetContentRegionAvail().x;
    const float barHeight = ImGui::GetFrameHeight();
    const float gap = std::round(0.3f * fontSize);
    const float tickLength = std::round(0.3f * fontSize);
    const std::string aboveLabel = scale.aboveLabel();
    const float boxWidth = std::round(std::max(1.6f * fontSize, ImGui::CalcTextSize(aboveLabel.c_str()).x + fontSize));
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float top = origin.y;
    const float bottom = top + barHeight;
    const float rampLeft = origin.x + boxWidth + gap;
    const float rampRight = origin.x + width - boxWidth - gap;
    const ImU32 outline = ImGui::GetColorU32(ImGuiCol_Border);
    const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
    const float textInBox = top + 0.5f * (barHeight - fontSize);

    const auto box = [&](float left, resolve::DisplayColor fill, resolve::DisplayColor ink, const std::string& label) {
        draw->AddRectFilled({left, top}, {left + boxWidth, bottom}, imguiColor(fill));
        draw->AddRect({left, top}, {left + boxWidth, bottom}, outline);
        const float labelWidth = ImGui::CalcTextSize(label.c_str()).x;
        draw->AddText({std::round(left + 0.5f * (boxWidth - labelWidth)), textInBox}, imguiColor(ink), label.c_str());
    };
    box(origin.x, resolve::kZeroCountColor, resolve::kAboveScaleColor, "0");
    box(origin.x + width - boxWidth, resolve::kAboveScaleColor, resolve::kZeroCountColor, aboveLabel);

    // The map, as horizontal gradients between closely spaced samples.
    constexpr int kSegments = 64;
    for (int i = 0; i < kSegments; ++i) {
        const float t0 = static_cast<float>(i) / kSegments;
        const float t1 = static_cast<float>(i + 1) / kSegments;
        const ImU32 c0 = imguiColor(resolve::viridis(t0));
        const ImU32 c1 = imguiColor(resolve::viridis(t1));
        draw->AddRectFilledMultiColor({rampLeft + t0 * (rampRight - rampLeft), top},
                                      {rampLeft + t1 * (rampRight - rampLeft), bottom}, c0, c1, c1, c0);
    }
    draw->AddRect({rampLeft, top}, {rampRight, bottom}, outline);

    // Ticks below the map. A label that would overlap the one before it is left out.
    float previousLabelRight = -FLT_MAX;
    for (const resolve::ScaleTick& tick : scale.ticks()) {
        const float x = std::round(rampLeft + tick.position * (rampRight - rampLeft));
        draw->AddLine({x, bottom}, {x, bottom + tickLength}, text);
        const float labelWidth = ImGui::CalcTextSize(tick.label.c_str()).x;
        const float labelLeft = std::clamp(x - 0.5f * labelWidth, rampLeft, rampRight - labelWidth);
        if (labelLeft < previousLabelRight + 0.5f * fontSize) {
            continue;
        }
        draw->AddText({std::round(labelLeft), bottom + tickLength}, text, tick.label.c_str());
        previousLabelRight = labelLeft + labelWidth;
    }

    ImGui::InvisibleButton("colour bar", {width, barHeight + tickLength + fontSize});
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    if (ImGui::IsItemHovered() && mouse.y <= bottom) {
        if (mouse.x >= rampLeft && mouse.x <= rampRight) {
            const float t = (mouse.x - rampLeft) / (rampRight - rampLeft);
            ImGui::SetTooltip("%s: %s", title, resolve::formatCount(scale.countAt(t)).c_str());
        } else if (mouse.x < rampLeft) {
            ImGui::SetTooltip("Black: count 0. The ray did none of this work.");
        } else {
            ImGui::SetTooltip("White: a count above the scale maximum.");
        }
    }
    if (ImGui::IsItemHovered() && mouse.y > bottom) {
        ImGui::SetTooltip("Colour map: viridis. Equal steps of the count look like equal steps of colour,\n"
                          "and lightness rises with the count. Readable with colour blindness and in grey.");
    }
}

// Where the frame is in the window: pixel (x, y) of the frame covers the window points from
// (left + x * scale, top + y * scale), in ImGui coordinates.
struct FrameInWindow {
    float left = 0.0f;
    float top = 0.0f;
    float scale = 1.0f; // window points for each pixel of the frame

    FrameInWindow(std::uint32_t width, std::uint32_t height)
    {
        const ImGuiIO& io = ImGui::GetIO();
        const ImVec2 framebufferScale = io.DisplayFramebufferScale;
        const VkExtent2D window{static_cast<std::uint32_t>(io.DisplaySize.x * framebufferScale.x),
                                static_cast<std::uint32_t>(io.DisplaySize.y * framebufferScale.y)};
        const std::array<VkOffset3D, 2> box = letterbox({width, height}, window);
        left = static_cast<float>(box[0].x) / framebufferScale.x;
        top = static_cast<float>(box[0].y) / framebufferScale.y;
        scale = static_cast<float>(box[1].x - box[0].x) / framebufferScale.x / static_cast<float>(width);
    }
};

const ViewInfo* findView(ShadingMode mode)
{
    for (const std::span<const ViewInfo> views : kViewGroups) {
        for (const ViewInfo& view : views) {
            if (view.mode == mode) {
                return &view;
            }
        }
    }
    return nullptr;
}

} // namespace

ControlPanel::ControlPanel(PanelFacts facts, const core::OrbitCamera& initialCamera, std::size_t initialTracer)
    : facts_(std::move(facts)),
      initialCamera_(initialCamera),
      camera_(initialCamera),
      activeTracer_(initialTracer),
      wantedEncoding_(facts_.encoding),
      wantedBrickSize_(facts_.brickSize),
      traceTimes_(facts_.tracers.size())
{
    shading_.distanceFar = 2.0f * initialCamera.distance;
    rememberEncodingSize();
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
    wantedEncoding_ = facts_.encoding;
    wantedBrickSize_ = facts_.brickSize;
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
        return "The memory baseline does not trace a surface.";
    if (mode == ShadingMode::WorkStructure && !tracer.countsStructureSteps)
        return "This method does not report the steps of its search structure.";
    if (!facts_.voxelCount &&
        (mode == ShadingMode::Normal || mode == ShadingMode::Faces || mode == ShadingMode::WorkCells ||
         mode == ShadingMode::WorkRuns || mode == ShadingMode::WorkHitCell))
        return "Open a voxel model to see this view.";
    if ((mode == ShadingMode::WorkMipmap || mode == ShadingMode::WorkSkipLevel) &&
        (facts_.encodings.empty() || !facts_.encodings.at(facts_.encoding).skipsEmptySpace))
        return "Choose an encoding with a min/max mipmap to see this view.";
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
    if (count > 0 && !measuring()) {
        wantedEncoding_ = static_cast<std::size_t>((static_cast<int>(wantedEncoding_) + count + step) % count);
        problem_.clear();
    }
}

void ControlPanel::selectNextBrickSize(int step)
{
    const auto count = static_cast<int>(facts_.brickSizes.size());
    if (count == 0 || measuring()) {
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
        reportProblem("Quick timing stopped: the window went to the background. Keep it in front and run it "
                      "again.");
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
            const FrameInWindow frame(facts_.width, facts_.height);
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
        stopInspecting();
        camera_ = initialCamera_;
        cameraMoved();
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
    for (const std::span<const ViewInfo> views : kViewGroups) {
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

void ControlPanel::draw()
{
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos({12, 12}, ImGuiCond_FirstUseEver);
    const float panelWidth = std::clamp(display.x * 0.34f, 360.f, 490.f);
    const float availableWidth = std::min(panelWidth, std::max(100.f, display.x - 24));
    ImGui::SetNextWindowSizeConstraints({availableWidth, 100}, {availableWidth, std::max(100.f, display.y - 24)});
    ImGui::SetNextWindowSize({std::min(466.f, display.x - 24), 0}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Voxel renderer", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    if (!problem_.empty())
        wrapped(problem_);
    exploring_ = false;
    if (ImGui::BeginTabBar("workspace")) {
        if (ImGui::BeginTabItem("Explore", nullptr,
                                std::exchange(openExplore_, false) ? ImGuiTabItemFlags_SetSelected : 0)) {
            exploring_ = true;
            drawExplore();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Measure", nullptr, measuring() ? ImGuiTabItemFlags_SetSelected : 0)) {
            drawComparison();
            ImGui::SeparatorText("Quick look (live window numbers, not for the thesis)");
            drawTracers();
            drawTimes();
            drawScene();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Controls")) {
            drawEncodings();
            drawViews();
            if (!facts_.alternateScene.empty() && ImGui::Button(facts_.alternateScene.c_str()) && !measuring())
                switchScene_ = true;
            wrapped("Left-drag to orbit. Ctrl + left-drag moves the light. Scroll to zoom. Hold the right button to "
                    "fly: mouse to look, W/A/S/D to "
                    "move, Q/E down and up, Shift faster, scroll for speed. A right click without moving picks "
                    "a pixel, Left and Right arrows step along its ray, V shows it from the side, Esc lets it go. S "
                    "shows the search structure. Tab changes the "
                    "search method, E the "
                    "encoding, B the brick size. R resets the camera.",
                    true);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
    drawInspectedPixelMark();

    // The choices of this frame are final now. recordTimings() gets its times in the next frame.
    frameInFlight_ =
        FrameInFlight{activeTracer_, cameraVersion_, countWork() && facts_.tracers.at(activeTracer_).countsWork};
    if (!frameInFlight_->countedWork) {
        shaderSeconds(activeTracer_) += static_cast<double>(ImGui::GetIO().DeltaTime);
    }
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

void ControlPanel::drawExplore()
{
    const bool voxels = facts_.voxelCount > 0;
    if (voxels) {
        ImGui::TextUnformatted(facts_.sceneName.c_str());
        ImGui::TextDisabled("%u bricks of %u x %u x %u voxels", facts_.primitiveCount, facts_.brickSize,
                            facts_.brickSize, facts_.brickSize);
    } else {
        ImGui::TextUnformatted("Box example");
        wrapped("Solid boxes instead of a voxel model, so that single hits are easy to see. Controls opens the voxel "
                "model.",
                true);
    }

    ImGui::SeparatorText("What you see");
    if (voxels) {
        for (const auto& [mode, label] : {std::pair{ShadingMode::Shaded, "Surface"},
                                          {ShadingMode::Faces, "Voxels"},
                                          {ShadingMode::PrimitiveId, "Bricks"}}) {
            if (mode != ShadingMode::Shaded)
                ImGui::SameLine();
            if (ImGui::RadioButton(label, shading_.mode == mode))
                selectView(mode);
        }
        if (shading_.mode == ShadingMode::Shaded)
            wrapped("The model, lit with the normal (surface direction) that is stored with each voxel.", true);
        else if (shading_.mode == ShadingMode::Faces)
            wrapped("Each visible voxel is drawn as a small cube with flat sides. Zoom in to see them.", true);
        else if (shading_.mode == ShadingMode::PrimitiveId)
            ImGui::TextWrapped("Each colour is one brick: a block of %u x %u x %u voxels that is stored together.",
                               facts_.brickSize, facts_.brickSize, facts_.brickSize);
        else
            wrapped("A view from the Controls tab is on.", true);
    } else {
        wrapped("The boxes, lit from their sides.", true);
    }
    drawLight();

    ImGui::SeparatorText("How the ray finds the surface");
    if (ImGui::BeginCombo("Search method", tracerLabel(facts_.tracers.at(activeTracer_).name))) {
        for (std::size_t i = 0; i < facts_.tracers.size(); ++i) {
            if (facts_.tracers[i].findsHits &&
                ImGui::Selectable(tracerLabel(facts_.tracers[i].name), i == activeTracer_))
                selectTracer(i);
        }
        ImGui::EndCombo();
    }
    if (!facts_.tracers.at(activeTracer_).findsHits) {
        wrapped("The memory baseline writes a miss for every pixel. Choose a search method.");
        return;
    }
    wrapped(facts_.tracers.at(activeTracer_).method.c_str(), true);
    structure_.controls(activeTracer_, facts_.tracers.at(activeTracer_).diagram, facts_.geometry, facts_.sceneBounds);

    ImGui::SeparatorText("Follow one ray");
    drawRayWalk();
}

void ControlPanel::drawRayWalk()
{
    if (!inspectedPixel_) {
        if (ImGui::Button(picking_ ? "Cancel" : "Pick a pixel"))
            picking_ = !picking_;
        wrapped(picking_ ? "Click the model or the background."
                         : "Pick a pixel to follow its ray step by step: the search, the brick, the voxel it hits.",
                true);
        return;
    }
    if (ImGui::Button("Pick another pixel")) {
        stopInspecting();
        picking_ = true;
        return;
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop (Esc)")) {
        stopInspecting();
        return;
    }
    if (!lesson_) {
        wrapped("Recording this ray on the GPU.", true);
        return;
    }
    // Stage bar: one button per stage, with the number of events. Hover: what the stage means.
    for (std::size_t s = 0; s < lesson_->stages.size(); ++s) {
        const RayStage& stage = lesson_->stages[s];
        if (s > 0)
            ImGui::SameLine(0.0f, 4.0f);
        const std::string label = stage.events.size() > 1
                                      ? std::format("{} ({})##stage{}", stage.name, stage.events.size(), s)
                                      : std::format("{}##stage{}", stage.name, s);
        const bool current = s == lessonStage_;
        if (current)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::SmallButton(label.c_str())) {
            lessonStage_ = s;
            lessonEvent_ = 0;
            orientCloseUp();
        }
        if (current)
            ImGui::PopStyleColor();
        ImGui::SetItemTooltip("%s", stage.tooltip.c_str());
    }
    ImGui::TextDisabled("%s", lesson_->stages.at(lessonStage_).summary.c_str());

    // Event stepper: one recorded event at a time (arrow keys step across stages).
    const bool atStart = lessonStage_ == 0 && lessonEvent_ == 0;
    const bool atEnd = lessonStage_ + 1 == lesson_->stages.size() &&
                       lessonEvent_ + 1 >= lesson_->stages.at(lessonStage_).events.size();
    ImGui::BeginDisabled(atStart);
    if (ImGui::ArrowButton("previous event", ImGuiDir_Left))
        previousRayStep();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(atEnd);
    if (ImGui::ArrowButton("next event", ImGuiDir_Right))
        nextRayStep();
    ImGui::EndDisabled();
    ImGui::SameLine();
    // The arrows can change the stage: read it only now, so that the event index belongs to it.
    const RayStage& stage = lesson_->stages.at(lessonStage_);
    if (stage.events.size() > 1) {
        int position = static_cast<int>(lessonEvent_) + 1;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
        if (ImGui::SliderInt("##event", &position, 1, static_cast<int>(stage.events.size()), "event %d"))
            lessonEvent_ = static_cast<std::size_t>(position - 1);
        ImGui::SameLine();
        ImGui::TextDisabled("/ %zu", stage.events.size());
        ImGui::SameLine();
    }
    if (ImGui::SmallButton(observing_ ? "Camera view (V)" : "Side view (V)"))
        toggleSideView();

    const RayEvent& event = stage.events.at(lessonEvent_);
    if (event.positive)
        ImGui::TextColored({1.0f, 0.8f, 0.26f, 1.0f}, "%s", event.label.c_str());
    else
        ImGui::TextUnformatted(event.label.c_str());

    if (stage.closeUp && lesson_->brickCell) {
        if (ImGui::SmallButton("Go to brick"))
            showBrick();
        ImGui::SetItemTooltip("Camera close to this brick, same direction as the captured view. "
                              "Camera view (V) returns.");
        drawBrickCloseUp(*lesson_, lessonStage_, lessonEvent_, closeUp_);
        ImGui::TextDisabled("grey: full voxels · faint cyan: brick cell · gold: ray, current column, hit voxel");
        if (stage.cumulative)
            ImGui::TextDisabled("blue: columns visited before · orange: mipmap square");
    }

    if (comparison_ && rayLog_ && !rayLog_->lostSteps) {
        ImGui::SeparatorText("Same pixel, two search methods");
        const auto showWork = [&](std::size_t method, const core::RayWork& work) {
            const PanelFacts::Tracer& tracer = facts_.tracers[method];
            const std::string& name = tracer.name;
            if (tracer.countsStructureSteps)
                ImGui::Text("%s: %u %s, %u bricks tested", tracerLabel(name), work.structureSteps,
                            tracer.structureStep.c_str(), work.primitiveTests);
            else
                ImGui::Text("%s: search steps hidden, %u bricks tested", tracerLabel(name), work.primitiveTests);
        };
        showWork(comparison_->first, comparison_->second);
        showWork(activeTracer_, rayLogWork_);
        wrapped("The surface is the same. The work to find it differs. The Measure tab shows what that costs for "
                "all pixels of the view.",
                true);
    }
    drawInspector();
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

void ControlPanel::drawComparison()
{
    ImGui::SeparatorText("Compare two settings at this view");
    wrapped("Runs the benchmark protocol on the exact camera rays of this view: warm-up, repeats, median. The "
            "result is saved as a results file, like the command line benchmark. The window pauses while it runs.",
            true);
    std::vector<std::string> choices;
    const bool surface = facts_.tracers.at(activeTracer_).findsHits;
    if (facts_.voxelCount && surface)
        choices.emplace_back();
    for (const auto& tracer : facts_.tracers)
        if (tracer.findsHits && tracer.name != tracerName() && surface)
            choices.push_back(tracer.name);
    if (!choices.empty()) {
        compareChoice_ = std::clamp(compareChoice_, 0, static_cast<int>(choices.size()) - 1);
        const auto label = [this](const std::string& name) -> const char* {
            return name.empty() ? "Empty-space skipping off / on" : tracerLabel(name);
        };
        if (ImGui::BeginCombo("Compare with", label(choices[compareChoice_]))) {
            for (int i = 0; i < static_cast<int>(choices.size()); ++i)
                if (ImGui::Selectable(label(choices[i]), i == compareChoice_))
                    compareChoice_ = i;
            ImGui::EndCombo();
        }
        ImGui::TextWrapped("Question: %s",
                           choices[compareChoice_].empty()
                               ? "does skipping empty columns (the min/max mipmap) make tracing this view faster?"
                               : "do both methods find the same surface, and which one takes less time for this view?");
        ImGui::BeginDisabled(measuring());
        if (ImGui::Button("Run the comparison"))
            compareRequest_ = choices[compareChoice_];
        ImGui::EndDisabled();
        if (compareRequest_)
            wrapped("Measuring both settings. The image stays still during warm-up and timed repeats.");
    }
    if (!result_)
        return;
    const auto& result = *result_;
    ImGui::SeparatorText("Result");
    const core::HitComparison& same = result.hits;
    if (same.agrees())
        ImGui::TextWrapped("Same surface: yes. %zu of %zu rays found the same hit (distances within %.0e, the "
                           "rule of the agreement test).",
                           same.rays - same.disagreements(), same.rays,
                           static_cast<double>(core::HitComparison::kRelativeTolerance));
    else
        ImGui::TextColored({1.0f, 0.45f, 0.35f, 1.0f}, "Same surface: NO. %zu of %zu rays found a different hit.",
                           same.disagreements(), same.rays);
    if (same.primitiveMismatch + same.attributeMismatch > 0)
        ImGui::TextDisabled("%zu rays hit two touching voxels at the same distance. Both answers are correct.",
                            same.primitiveMismatch + same.attributeMismatch);
    const auto& first = result.rows.front();
    const auto& second = result.rows.back();
    const bool skippingPair = first.encoding != second.encoding;
    if (skippingPair && first.medianMs > 0.0) {
        ImGui::TextWrapped(
            "With skipping, a ray checked %+.2f columns on average, and the median trace time changed by %+.1f %%.",
            second.cellsPerRay - first.cellsPerRay, 100.0 * (second.medianMs / first.medianMs - 1.0));
    }
    for (const auto& row : result.rows) {
        ImGui::PushID(row.file.string().c_str());
        if (skippingPair)
            ImGui::Text("%s  |  Skipping %s", tracerLabel(row.tracer), encodingSkips(row.encoding) ? "on" : "off");
        else
            ImGui::TextUnformatted(tracerLabel(row.tracer));
        ImGui::Text("Model %s   Search structure %s", formatBytes(row.sceneBytes).c_str(),
                    formatBytes(row.structureBytes).c_str());
        ImGui::Text("Trace %.3f ms (median)   spread between repeats %.1f %%", row.medianMs, row.spreadPercent);
        if (!row.settled)
            ImGui::TextColored({1.0f, 0.45f, 0.35f, 1.0f},
                               "Warm-up ended before the GPU clock and the time were steady: run it again.");
        if (facts_.voxelCount)
            ImGui::Text("Per ray: %.2f columns visited, %.2f squares skipped", row.cellsPerRay, row.skipsPerRay);
        if (ImGui::SmallButton("Open this view in Explore")) {
            stopInspecting();
            camera_ = result.camera;
            cameraMoved();
            for (std::size_t i = 0; i < facts_.tracers.size(); ++i)
                if (facts_.tracers[i].name == row.tracer)
                    selectTracer(i);
            for (std::size_t i = 0; i < facts_.encodings.size(); ++i)
                if (facts_.encodings[i].name == row.encoding)
                    wantedEncoding_ = i;
            wantedBrickSize_ = resultBrickSize_;
            selectView(ShadingMode::Shaded);
            picking_ = true;
            pickAfterReload_ = wantedEncoding_ != facts_.encoding || wantedBrickSize_ != facts_.brickSize;
            openExplore_ = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy result path"))
            ImGui::SetClipboardText(std::filesystem::absolute(row.file).string().c_str());
        ImGui::PopID();
    }
}

void ControlPanel::drawTracers()
{
    if (!ImGui::CollapsingHeader("Tracers", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    const std::optional<double> floor = floorMilliseconds();
    bool anyFromNewShader = false;
    if (ImGui::BeginTable("tracers", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Tracer", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Structure", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Build", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Trace ms", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Over baseline", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < facts_.tracers.size(); ++i) {
            const PanelFacts::Tracer& tracer = facts_.tracers[i];
            const TraceTime& time = traceTimes_[i];
            const bool current = time.cameraVersion == cameraVersion_;
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            if (ImGui::Selectable(tracerLabel(tracer.name), i == activeTracer_, ImGuiSelectableFlags_SpanAllColumns) &&
                !measuring()) {
                selectTracer(i);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", tracer.description.c_str());
            }

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(formatBytes(tracer.memoryBytes).c_str());

            ImGui::TableNextColumn();
            if (tracer.buildMilliseconds > 0.0) {
                ImGui::Text("%.1f ms %s", tracer.buildMilliseconds, tracer.buildOnCpu ? "CPU" : "GPU");
            } else {
                ImGui::TextDisabled("-");
            }

            ImGui::TableNextColumn();
            const char* mark = time.fromNewShader ? " *" : "";
            if (time.cameraVersion == 0) {
                ImGui::TextDisabled("-");
            } else if (current && !time.fromNewShader) {
                ImGui::Text("%.3f", time.milliseconds);
            } else {
                ImGui::TextDisabled("%.3f%s%s", time.milliseconds, mark, current ? "" : " (old view)");
            }
            anyFromNewShader = anyFromNewShader || (time.cameraVersion != 0 && time.fromNewShader);

            ImGui::TableNextColumn();
            if (tracer.findsHits && floor && time.cameraVersion != 0 && !time.fromNewShader) {
                if (current) {
                    ImGui::Text("%.3f", time.milliseconds - *floor);
                } else {
                    ImGui::TextDisabled("%.3f", time.milliseconds - *floor);
                }
            } else {
                ImGui::TextDisabled("-");
            }
        }
        ImGui::EndTable();
    }

    wrapped(facts_.tracers.at(activeTracer_).description);
    for (const std::string& skipped : facts_.skippedTracers) {
        wrapped("Not available: " + skipped, true);
    }

    if (measuring()) {
        const std::size_t tracer = measurement_->tracer;
        ImGui::Text("Measuring %s (%zu of %zu)", facts_.tracers.at(tracer).name.c_str(), tracer + 1,
                    facts_.tracers.size());
        ImGui::SameLine();
        if (ImGui::SmallButton("Stop")) {
            stopMeasuring();
        } else if (shaderIsNew(tracer)) {
            ImGui::Text("Its shader is new: it runs for %.0f more seconds first.",
                        kNewShaderSeconds - shaderSeconds(tracer));
        }
    } else if (!windowFocused_) {
        ImGui::TextDisabled("Window in the background: timing paused.");
    } else if (ImGui::Button("Quick timing (M)")) {
        measureAll();
    }
    if (anyFromNewShader) {
        wrapped("*: the shader of this tracer ran for less than 7 seconds. The driver runs a new shader slower at "
                "first, so this time is too high.",
                true);
    }
    wrapped("\"Quick timing\" runs each tracer for some frames at this camera and fills the column \"Trace ms\". "
            "\"Over baseline\" subtracts the time to read the rays and write misses. Old view means the camera moved. "
            "These are live numbers of the window. Use Compare this view for a saved benchmark.",
            true);
}

void ControlPanel::drawEncodings()
{
    if (facts_.encodings.empty() || !ImGui::CollapsingHeader("Encodings", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    bool sizeUnknown = false;
    if (ImGui::BeginTable("encodings", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Encoding", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Size on the GPU", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Bits per voxel", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < facts_.encodings.size(); ++i) {
            const PanelFacts::Encoding& encoding = facts_.encodings[i];
            const std::optional<EncodingSize>& size = encodingSizes_[i];
            ImGui::TableNextRow();

            // The scene on screen belongs to facts_.encoding. A new choice takes effect in the next frame.
            ImGui::TableNextColumn();
            if (ImGui::Selectable(encoding.name.c_str(), i == wantedEncoding_, ImGuiSelectableFlags_SpanAllColumns) &&
                !measuring()) {
                wantedEncoding_ = i;
                problem_.clear();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", encoding.description.c_str());
            }

            ImGui::TableNextColumn();
            if (size) {
                ImGui::TextUnformatted(formatBytes(size->bytes).c_str());
            } else {
                ImGui::TextDisabled("-");
            }
            ImGui::TableNextColumn();
            if (size) {
                ImGui::Text("%.2f", size->bitsPerVoxel);
            } else {
                ImGui::TextDisabled("-");
            }
            sizeUnknown = sizeUnknown || !size;
        }
        ImGui::EndTable();
    }
    wrapped(facts_.encodings.at(facts_.encoding).description);
    if (sizeUnknown) {
        wrapped("\"-\": select the encoding once to see its size with this brick size.", true);
    }

    if (!facts_.brickSizes.empty()) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Brick size (B):");
        for (const std::uint32_t size : facts_.brickSizes) {
            ImGui::SameLine();
            if (ImGui::RadioButton(std::to_string(size).c_str(), wantedBrickSize_ == size) && !measuring()) {
                wantedBrickSize_ = size;
                problem_.clear();
            }
        }
        wrapped("A brick is one box for the search structure of a tracer. Smaller bricks give more and smaller boxes.",
                true);
    }
    if (!problem_.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored({1.0f, 0.45f, 0.35f, 1.0f}, "%s", problem_.c_str());
        ImGui::PopTextWrapPos();
    }
}

void ControlPanel::drawViews()
{
    if (!ImGui::CollapsingHeader("Views", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    const auto list = [&](const char* title, std::span<const ViewInfo> views) {
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", title);
        for (const ViewInfo& view : views) {
            char label[64];
            std::snprintf(label, sizeof(label), "%s  %s", view.keyName, view.name);
            const char* reason = unavailableView(view.mode);
            ImGui::BeginDisabled(reason != nullptr);
            if (ImGui::Selectable(label, shading_.mode == view.mode)) {
                selectView(view.mode);
            }
            ImGui::EndDisabled();
            if (reason && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", reason);
        }
    };
    if (ImGui::BeginTable("views", 2)) {
        list("The hit", kImageViews);
        list("The work of each ray", kWorkViews);
        ImGui::EndTable();
    }

    if (const ViewInfo* view = findView(shading_.mode)) {
        wrapped(!facts_.voxelCount && shading_.mode == ShadingMode::Shaded
                    ? "Light falls on the visible faces of the solid boxes."
                    : view->legend);
    }
    drawLight();
    if (shading_.mode == ShadingMode::Distance) {
        ImGui::DragFloatRange2("Distance range", &shading_.distanceNear, &shading_.distanceFar, 0.1f, 0.0f, 1000.0f);
    }
    const bool workView = resolve::showsWork(shading_.mode);
    if (resolve::showsCount(shading_.mode)) {
        resolve::ColorScale& scale = shading_.workScale;
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
        ImGui::DragFloat("Scale maximum", &scale.maximum, 1.0f, 1.0f, 4096.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("The count at the end of the colour map. A larger count is white.");
        ImGui::SameLine();
        if (ImGui::RadioButton("linear", scale.kind == resolve::ScaleKind::Linear))
            scale.kind = resolve::ScaleKind::Linear;
        ImGui::SameLine();
        if (ImGui::RadioButton("log2", scale.kind == resolve::ScaleKind::Log2))
            scale.kind = resolve::ScaleKind::Log2;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Position on the map: log2(count) / log2(maximum). Shows the small counts and the\n"
                              "long tail of large counts in one image.");
        drawColorBar(scale, resolve::countedQuantity(shading_.mode));
    }
    if (workView && !facts_.tracers.at(activeTracer_).countsWork) {
        wrapped("This tracer does not count work. The image shows the shaded view.", true);
    } else if (workView) {
        wrapped("A work view runs the counting variant of the shader. It is slower than the tracer, so the table "
                "of the tracers gets no time from these frames.",
                true);
        drawWorkSummary();
    }
}

void ControlPanel::moveLight(float azimuthRadians, float elevationRadians)
{
    constexpr float kTwoPi = 6.28318531f;
    constexpr float kMaxElevation = 1.5707963f; // 90 deg: straight above or below
    resolve::LightDirection& light = shading_.light;
    light.azimuth = std::remainder(light.azimuth + azimuthRadians, kTwoPi);
    light.elevation = std::clamp(light.elevation + elevationRadians, -kMaxElevation, kMaxElevation);
}

void ControlPanel::drawLight()
{
    if (!resolve::usesLight(shading_.mode))
        return;
    resolve::LightDirection& light = shading_.light;
    drawLightSphere();
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
    ImGui::SliderAngle("Light azimuth", &light.azimuth, -180.0f, 180.0f, "%.0f deg");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Angle of the light around the vertical axis (y). 0 deg: from +z.\n"
                          "Ctrl + left-drag in the image also moves the light.");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
    ImGui::SliderAngle("Light elevation", &light.elevation, -90.0f, 90.0f, "%.0f deg");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Angle of the light above the horizontal plane. 90 deg: straight from above.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset light"))
        light = resolve::LightDirection{};
    ImGui::EndGroup();
}

void ControlPanel::drawLightSphere()
{
    // A sphere seen from the current camera, lit with the same rule and display curve as the
    // lit views (shade.slang). The dot marks the light: filled when the light is on the camera
    // side of the sphere, a ring when it is behind. A click or drag on the sphere puts the light
    // above the point under the mouse.
    resolve::LightDirection& light = shading_.light;
    const core::CameraFrame frame = camera_.frame(1.0f);
    const glm::vec3 right = glm::normalize(frame.right);
    const glm::vec3 up = glm::normalize(frame.up);
    const glm::vec3 back = -glm::normalize(frame.forward); // towards the viewer
    const float horizontal = std::cos(light.elevation);
    const glm::vec3 toLight{horizontal * std::sin(light.azimuth), std::sin(light.elevation),
                            horizontal * std::cos(light.azimuth)};
    const glm::vec3 lightInView{glm::dot(toLight, right), glm::dot(toLight, up), glm::dot(toLight, back)};

    const float size = ImGui::GetFrameHeight() * 2.0f + ImGui::GetStyle().ItemSpacing.y;
    const ImVec2 corner = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##light sphere", ImVec2(size, size));
    const float radius = 0.5f * size - 1.0f;
    const ImVec2 centre(corner.x + 0.5f * size, corner.y + 0.5f * size);

    if (ImGui::IsItemActive()) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        float x = (mouse.x - centre.x) / radius;
        float y = (centre.y - mouse.y) / radius;
        const float length = std::sqrt(x * x + y * y);
        if (length > 1.0f) {
            x /= length;
            y /= length;
        }
        const float z = std::sqrt(std::max(0.0f, 1.0f - x * x - y * y));
        const glm::vec3 world = glm::normalize(x * right + y * up + z * back);
        light.azimuth = std::atan2(world.x, world.z);
        light.elevation = std::asin(std::clamp(world.y, -1.0f, 1.0f));
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("The light on a sphere, seen from the camera.\n"
                          "Filled dot: light in front. Ring: light behind.\n"
                          "Click or drag on the sphere to put the light there.");

    ImDrawList* draw = ImGui::GetWindowDrawList();
    constexpr int kCells = 28;
    const float cell = 2.0f * radius / kCells;
    for (int row = 0; row < kCells; ++row) {
        for (int column = 0; column < kCells; ++column) {
            const float x = -1.0f + (column + 0.5f) * 2.0f / kCells;
            const float y = 1.0f - (row + 0.5f) * 2.0f / kCells;
            const float rr = x * x + y * y;
            if (rr > 1.0f)
                continue;
            const glm::vec3 normal{x, y, std::sqrt(1.0f - rr)};
            const float linear = 0.8f * (0.25f + 0.75f * std::max(glm::dot(normal, lightInView), 0.0f));
            const auto grey = static_cast<int>(255.0f * std::pow(linear, 1.0f / 2.2f) + 0.5f);
            const ImVec2 min(centre.x - radius + column * cell, centre.y - radius + row * cell);
            draw->AddRectFilled(min, ImVec2(min.x + cell + 0.5f, min.y + cell + 0.5f), IM_COL32(grey, grey, grey, 255));
        }
    }
    draw->AddCircle(centre, radius, IM_COL32(90, 100, 115, 255), 48, 1.0f);

    const ImVec2 dot(centre.x + lightInView.x * radius, centre.y - lightInView.y * radius);
    const ImU32 yellow = IM_COL32(255, 205, 50, 255);
    if (lightInView.z >= 0.0f) {
        draw->AddCircleFilled(dot, 4.0f, yellow);
        draw->AddCircle(dot, 4.0f, IM_COL32(40, 30, 0, 255), 12, 1.0f);
    } else {
        draw->AddCircle(dot, 4.0f, yellow, 12, 2.0f);
    }
}

void ControlPanel::drawWorkSummary()
{
    if (!workSummary_ || workSummary_->rays() == 0) {
        ImGui::TextDisabled("Average work of a ray: comes with the next frames.");
        return;
    }
    const RayWorkSummary& summary = *workSummary_;
    ImGui::Text("Average work of a ray in the frame (%.1f %% of the rays hit):",
                100.0 * static_cast<double>(summary.raysWithHit()) / static_cast<double>(summary.rays()));
    const RayWorkSummary::Means all = summary.meanPerRay();
    const RayWorkSummary::Means hit = summary.meanPerRayWithHit();
    const core::RayWork& most = summary.most();
    if (ImGui::BeginTable("work", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("a ray", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("a ray that hits", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("most", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();
        const auto row = [](const char* name, double perRay, double perHit, std::uint32_t largest) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(name);
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", perRay);
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", perHit);
            ImGui::TableNextColumn();
            ImGui::Text("%u", largest);
        };
        if (facts_.tracers.at(activeTracer_).countsStructureSteps)
            row("search structure steps", all.structureSteps, hit.structureSteps, most.structureSteps);
        row("primitive tests", all.primitiveTests, hit.primitiveTests, most.primitiveTests);
        row("mipmap cells read", all.mipmapCells, hit.mipmapCells, most.mipmapCells);
        row("  of them skipped", all.mipmapSkips, hit.mipmapSkips, most.mipmapSkips);
        row("cells visited", all.cells, hit.cells, most.cells);
        row("runs tested", all.runs, hit.runs, most.runs);
        ImGui::EndTable();
    }
    if (most.skippedLevels != 0) {
        std::string text = "Rays by the largest square skipped:";
        const auto& byLevel = summary.raysByLargestSkip();
        for (std::size_t level = 0; level < byLevel.size(); ++level) {
            if (byLevel[level] == 0) {
                continue;
            }
            char part[64];
            const double percent = 100.0 * static_cast<double>(byLevel[level]) / static_cast<double>(summary.rays());
            if (level == 0) {
                std::snprintf(part, sizeof(part), " none %.1f %%", percent);
            } else {
                const unsigned size = 1u << level;
                std::snprintf(part, sizeof(part), ", %ux%u %.1f %%", size, size, percent);
            }
            text += part;
        }
        wrapped(text, true);
    }
    wrapped("Updated about once a second. Numbers for the thesis come from --benchmark (summary.ray_work).", true);
}

void ControlPanel::drawInspector()
{
    if (!ImGui::CollapsingHeader("Every recorded step (technical)")) {
        return;
    }
    if (!inspectedPixel_) {
        wrapped("Right click on the image: the panel lists every step of the ray of that pixel. It shows the search "
                "structure, the primitives, and inside a brick the mipmap squares, the cells and the runs.",
                true);
        return;
    }
    const PanelFacts::Tracer& tracer = facts_.tracers.at(activeTracer_);
    if (!tracer.countsWork) {
        wrapped("This tracer does not count work, so it writes no steps.", true);
        return;
    }
    if (!rayLog_) {
        ImGui::TextDisabled("The steps come with the next frame.");
        return;
    }

    const core::RayWork& work = rayLogWork_;
    if (tracer.countsStructureSteps)
        ImGui::Text("%u search structure steps", work.structureSteps);
    ImGui::Text("%u candidate boxes tested", work.primitiveTests);
    if (rayLog_->lostSteps)
        wrapped("The log was truncated. Counts here cover the recorded events only.");
    if (work.mipmapCells > 0) {
        const std::uint32_t largest =
            work.skippedLevels == 0 ? 0u : static_cast<std::uint32_t>(std::bit_width(work.skippedLevels) - 1);
        if (largest > 0) {
            ImGui::Text("%u mipmap cells read, %u squares skipped, the largest %ux%u cells", work.mipmapCells,
                        work.mipmapSkips, 1u << largest, 1u << largest);
        } else {
            ImGui::Text("%u mipmap cells read, no square skipped", work.mipmapCells);
        }
    }
    ImGui::Text("%u cells visited, %u runs tested", work.cells, work.runs);
    if (!tracer.countsStructureSteps) {
        wrapped("This method does not report which parts of its search structure the ray visits. The steps start "
                "at the primitives that the search hands to the shader.",
                true);
    }

    ImGui::Checkbox("Only hits and skipped squares", &onlyImportantSteps_);
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy as text")) {
        ImGui::SetClipboardText(rayLogAsText(rayLogLines_).c_str());
    }

    std::vector<std::size_t> shown;
    shown.reserve(rayLogLines_.size());
    for (std::size_t i = 0; i < rayLogLines_.size(); ++i) {
        if (!onlyImportantSteps_ || rayLogLines_[i].important) {
            shown.push_back(i);
        }
    }
    const ImVec4 strong{1.0f, 0.85f, 0.35f, 1.0f};
    if (ImGui::BeginTable("steps", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV,
                          {0.0f, 18.0f * ImGui::GetTextLineHeightWithSpacing()})) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("t", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("step", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(shown.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const RayLogLine& line = rayLogLines_[shown[static_cast<std::size_t>(row)]];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (line.hasT) {
                    ImGui::TextDisabled("%.4f", line.t);
                }
                ImGui::TableNextColumn();
                const float indent = 14.0f * static_cast<float>(line.depth) + 1.0f;
                ImGui::Indent(indent);
                if (line.important) {
                    ImGui::TextColored(strong, "%s", line.text.c_str());
                } else {
                    ImGui::TextUnformatted(line.text.c_str());
                }
                ImGui::Unindent(indent);
            }
        }
        ImGui::EndTable();
    }
    wrapped("t: the distance along the ray where the step begins. Cells (u, v) are cells of the primary grid of the "
            "brick; heights count voxels along its height axis. These events belong to the selected pixel at capture.",
            true);
}

void ControlPanel::drawInspectedPixelMark() const
{
    const FrameInWindow frame(facts_.width, facts_.height);
    const float width = static_cast<float>(facts_.width), height = static_cast<float>(facts_.height);
    // The depth map belongs to one camera position. While the camera moves, lines are drawn plain.
    const OverlayPainter painter(ImGui::GetBackgroundDrawList(), camera_.frame(width / height), frame.left, frame.top,
                                 width * frame.scale, height * frame.scale,
                                 depthVersion_ == cameraVersion_ ? &depth_ : nullptr);
    if (exploring_ && facts_.tracers.at(activeTracer_).findsHits)
        structure_.draw(painter);
    if (!inspectedPixel_)
        return;
    if (lesson_) {
        drawRayLesson(*lesson_, lessonStage_, lessonEvent_, painter);
        if (observing_)
            return;
    }
    const ImVec2 centre{frame.left + (static_cast<float>(inspectedPixel_->first) + 0.5f) * frame.scale,
                        frame.top + (static_cast<float>(inspectedPixel_->second) + 0.5f) * frame.scale};
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    draw->AddCircle(centre, 9.0f, IM_COL32(0, 0, 0, 255), 0, 4.0f);
    draw->AddCircle(centre, 9.0f, IM_COL32(255, 230, 60, 255), 0, 2.0f);
}

void ControlPanel::drawTimes()
{
    if (!ImGui::CollapsingHeader("GPU time of the frame on screen", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    const double rays = static_cast<double>(facts_.width) * facts_.height;
    const double megaRays = traceMs_.value > 0.0 ? rays / (traceMs_.value * 1e3) : 0.0;
    ImGui::Text("ray generation %8.3f ms", rayGenerationMs_.value);
    ImGui::Text("trace          %8.3f ms  (%.0f Mrays/s)", traceMs_.value, megaRays);
    wrapped(lastFrameCounted_ ? "Trace time includes diagnostic counting."
                              : "Trace time covers all rays in this view with diagnostic counting off.",
            true);
    ImGui::Text("shading        %8.3f ms", shadingMs_.value);
    ImGui::Text("%ux%u rays for each frame", facts_.width, facts_.height);
    if (ImGui::Button("Reset camera (R)") && !measuring()) {
        stopInspecting();
        camera_ = initialCamera_;
        cameraMoved();
    }
}

void ControlPanel::drawScene()
{
    if (!ImGui::CollapsingHeader("Scene and memory")) {
        return;
    }
    ImGui::Text("%s", facts_.sceneName.c_str());
    ImGui::Text("%s", facts_.sceneDetails.c_str());
    if (facts_.voxelCount > 0) {
        ImGui::Text("%llu voxels in %u bricks", static_cast<unsigned long long>(facts_.voxelCount),
                    facts_.primitiveCount);
    } else {
        ImGui::Text("%u primitives", facts_.primitiveCount);
    }

    for (const PanelFacts::SceneBuffer& buffer : facts_.sceneBuffers) {
        ImGui::Text("%-16s %s", buffer.name.c_str(), formatBytes(buffer.bytes).c_str());
    }
    if (facts_.voxelCount > 0) {
        ImGui::Text("%-16s %s  (%.2f bits per voxel)", "scene total", formatBytes(facts_.sceneBytes).c_str(),
                    8.0 * static_cast<double>(facts_.sceneBytes) / static_cast<double>(facts_.voxelCount));
        ImGui::Text("%-16s %.1f ms", "encode time", facts_.encodeMilliseconds);
    } else {
        ImGui::Text("%-16s %s", "scene total", formatBytes(facts_.sceneBytes).c_str());
    }
    ImGui::Text("%-16s %s", "rays + hits", formatBytes(facts_.rayAndHitBytes).c_str());
}

} // namespace thesis::app
