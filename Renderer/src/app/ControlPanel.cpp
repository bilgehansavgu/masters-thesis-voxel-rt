#include "ControlPanel.h"

#include "FramePipeline.h"
#include "Presenter.h"

#include "thesis/core/Statistics.h"
#include "thesis/shared/RayLog.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <span>
#include <string_view>
#include <utility>

namespace thesis::app {
namespace {

// "Measure all": the first frames of each tracer are not used, the frames after them are.
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
    const std::string previous = facts_.tracers.at(activeTracer_).name;
    facts_ = std::move(facts);
    const auto same = std::find_if(facts_.tracers.begin(), facts_.tracers.end(),
                                   [&](const PanelFacts::Tracer& tracer) { return tracer.name == previous; });
    activeTracer_ = same == facts_.tracers.end() ? 0 : static_cast<std::size_t>(same - facts_.tracers.begin());
    wantedEncoding_ = facts_.encoding;
    wantedBrickSize_ = facts_.brickSize;

    // The old times belong to the old scene.
    traceTimes_.assign(facts_.tracers.size(), {});
    measurement_.reset();
    frameInFlight_.reset();
    traceMs_ = {};
    rayLog_.reset();
    rayLogLines_.clear();
    workSummary_.reset();
    rememberEncodingSize();
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
        activeTracer_ = static_cast<std::size_t>((static_cast<int>(activeTracer_) + count + step) % count);
    }
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

void ControlPanel::measureAll()
{
    if (facts_.tracers.empty() || measuring()) {
        return;
    }
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
    if (x < facts_.width && y < facts_.height) {
        inspectedPixel_ = {x, y};
        rayLog_.reset();
        rayLogLines_.clear();
    }
}

void ControlPanel::stopInspecting()
{
    inspectedPixel_.reset();
    rayLog_.reset();
    rayLogLines_.clear();
}

std::optional<std::uint32_t> ControlPanel::loggedRay() const
{
    if (!inspectedPixel_) {
        return std::nullopt;
    }
    return inspectedPixel_->second * facts_.width + inspectedPixel_->first;
}

void ControlPanel::recordRayLog(std::optional<tracing::RayLog> log)
{
    if (!inspectedPixel_ || !log || log->ray != loggedRay()) {
        return; // keep what is shown until a log of this ray comes
    }
    rayLog_ = std::move(log);
    rayLogLines_ = describeRayLog(*rayLog_);
    rayLogWork_ = {};
    for (const core::RayLogEntry& step : rayLog_->steps) {
        switch (step.kind) {
        case THESIS_LOG_GRID_CELL:
        case THESIS_LOG_OCTREE_NODE:
        case THESIS_LOG_BVH_NODE:
            ++rayLogWork_.structureSteps;
            break;
        case THESIS_LOG_PRIMITIVE:
            ++rayLogWork_.primitiveTests;
            break;
        case THESIS_LOG_MIPMAP:
            ++rayLogWork_.mipmapCells;
            if (step.f == 1) {
                ++rayLogWork_.mipmapSkips;
                rayLogWork_.skippedLevels |= 1u << step.a;
            }
            break;
        case THESIS_LOG_CELL:
            ++rayLogWork_.cells;
            break;
        case THESIS_LOG_RUN:
            ++rayLogWork_.runs;
            break;
        default:
            break;
        }
    }
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

    if (!io.WantCaptureMouse) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)) {
            camera_.yawDegrees -= 0.25f * io.MouseDelta.x;
            camera_.pitchDegrees = std::clamp(camera_.pitchDegrees + 0.25f * io.MouseDelta.y, -89.0f, 89.0f);
            cameraMoved();
        }
        if (io.MouseWheel != 0.0f) {
            camera_.distance = std::max(0.1f, camera_.distance * (1.0f - 0.1f * io.MouseWheel));
            cameraMoved();
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && facts_.width > 0) {
            const FrameInWindow frame(facts_.width, facts_.height);
            const float x = (io.MousePos.x - frame.left) / frame.scale;
            const float y = (io.MousePos.y - frame.top) / frame.scale;
            if (x >= 0.0f && y >= 0.0f) {
                inspectPixel(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
            }
        }
    }
    if (io.WantCaptureKeyboard) {
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
        camera_ = initialCamera_;
        cameraMoved();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        stopInspecting();
    }
    for (const std::span<const ViewInfo> views : kViewGroups) {
        for (const ViewInfo& view : views) {
            if (ImGui::IsKeyPressed(view.key, false)) {
                shading_.mode = view.mode;
            }
        }
    }
}

void ControlPanel::recordTimings(const std::vector<gpu::StageTiming>& timings)
{
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
    ImGui::SetNextWindowPos({10, 10}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({560, 0}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Renderer");

    wrapped("Mouse: drag turns the camera, the wheel moves it nearer or farther.", true);
    wrapped("Keys: Tab tracer, E encoding, B brick size (with Shift: the one before). 1-9, 0, - and = view. "
            "M measure all. R reset the camera. Right click: look at the steps of one ray (Esc: let it go).",
            true);

    drawTracers();
    drawEncodings();
    drawViews();
    drawInspector();
    drawTimes();
    drawScene();
    ImGui::End();
    drawInspectedPixelMark();

    // The choices of this frame are final now. recordTimings() gets its times in the next frame.
    frameInFlight_ =
        FrameInFlight{activeTracer_, cameraVersion_, countWork() && facts_.tracers.at(activeTracer_).countsWork};
    if (!frameInFlight_->countedWork) {
        shaderSeconds(activeTracer_) += static_cast<double>(ImGui::GetIO().DeltaTime);
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
        ImGui::TableSetupColumn("Above null", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < facts_.tracers.size(); ++i) {
            const PanelFacts::Tracer& tracer = facts_.tracers[i];
            const TraceTime& time = traceTimes_[i];
            const bool current = time.cameraVersion == cameraVersion_;
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            if (ImGui::Selectable(tracer.name.c_str(), i == activeTracer_, ImGuiSelectableFlags_SpanAllColumns) &&
                !measuring()) {
                activeTracer_ = i;
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
                ImGui::TextDisabled("%.3f%s", time.milliseconds, mark);
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
    } else if (ImGui::Button("Measure all (M)")) {
        measureAll();
    }
    if (anyFromNewShader) {
        wrapped("*: the shader of this tracer ran for less than 7 seconds. The driver runs a new shader slower at "
                "first, so this time is too high.",
                true);
    }
    wrapped("\"Measure all\" runs each tracer for some frames at this camera and fills the column \"Trace ms\". "
            "\"Above null\" is the time minus the time of the null tracer. Grey: from another camera position. "
            "These are live numbers of the window. Numbers for the thesis come from --benchmark.",
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
            if (ImGui::Selectable(label, shading_.mode == view.mode)) {
                shading_.mode = view.mode;
            }
        }
    };
    if (ImGui::BeginTable("views", 2)) {
        list("The hit", kImageViews);
        list("The work of each ray", kWorkViews);
        ImGui::EndTable();
    }

    if (const ViewInfo* view = findView(shading_.mode)) {
        wrapped(view->legend);
    }
    if (shading_.mode == ShadingMode::Distance) {
        ImGui::DragFloatRange2("Distance range", &shading_.distanceNear, &shading_.distanceFar, 0.1f, 0.0f, 1000.0f);
    }
    const bool workView = resolve::showsWork(shading_.mode);
    if (workView && shading_.mode != ShadingMode::WorkHitCell) {
        ImGui::DragFloat("Count at red", &shading_.workScale, 1.0f, 1.0f, 4096.0f, "%.0f");
        wrapped("Colours: black 0, then blue, green, yellow, red. White: more than the count at red.", true);
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
    if (!ImGui::CollapsingHeader("Ray inspector", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    if (!inspectedPixel_) {
        wrapped("Right click on the image: the panel lists every step of the ray of that pixel. It shows the search "
                "structure, the primitives, and inside a brick the mipmap squares, the cells and the runs.",
                true);
        return;
    }
    ImGui::Text("Pixel (%u, %u), ray %u", inspectedPixel_->first, inspectedPixel_->second, *loggedRay());
    ImGui::SameLine();
    if (ImGui::SmallButton("Let go (Esc)")) {
        stopInspecting();
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
    ImGui::Text("%u search structure steps, %u primitive tests", work.structureSteps, work.primitiveTests);
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
    if (tracer.name == "hardware") {
        wrapped("The GPU does not tell which nodes of its BVH the ray visits. The steps start at the primitives "
                "that the GPU hands to the shader.",
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
            "brick; heights count voxels along its height axis. The inspector runs the counting variant of the "
            "shader, so the table of the tracers gets no time while it is on.",
            true);
}

void ControlPanel::drawInspectedPixelMark() const
{
    if (!inspectedPixel_) {
        return;
    }
    const FrameInWindow frame(facts_.width, facts_.height);
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
    ImGui::Text("shading        %8.3f ms", shadingMs_.value);
    ImGui::Text("%ux%u rays for each frame", facts_.width, facts_.height);
    if (ImGui::Button("Reset camera (R)") && !measuring()) {
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
