#include "PanelWidgets.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <iterator>

namespace thesis::app {
namespace {

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

static_assert(std::size(kImageViews) + std::size(kWorkViews) == resolve::kShadingModeNames.size());

ImU32 imguiColor(resolve::DisplayColor color)
{
    return ImGui::ColorConvertFloat4ToU32({color.r, color.g, color.b, 1.0f});
}

} // namespace

std::span<const ViewInfo> imageViews()
{
    return kImageViews;
}

std::span<const ViewInfo> workViews()
{
    return kWorkViews;
}

const ViewInfo* findView(ShadingMode mode)
{
    for (const std::span<const ViewInfo> views : {imageViews(), workViews()}) {
        for (const ViewInfo& view : views) {
            if (view.mode == mode) {
                return &view;
            }
        }
    }
    return nullptr;
}

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

void wrapped(const std::string& text, bool disabled)
{
    ImGui::PushTextWrapPos(0.0f);
    if (disabled) {
        ImGui::TextDisabled("%s", text.c_str());
    } else {
        ImGui::TextUnformatted(text.c_str());
    }
    ImGui::PopTextWrapPos();
}

void helpMarker(const char* text)
{
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

bool sectionHeader(const char* title, bool openByDefault)
{
    ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive));
    const bool open = ImGui::CollapsingHeader(title, openByDefault ? ImGuiTreeNodeFlags_DefaultOpen : 0);
    ImGui::PopStyleColor(3);
    return open;
}

void rowLabel(const char* label)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(ImGui::GetFontSize() * 6.5f);
}

float drawColorBar(ImDrawList* draw, ImVec2 origin, float width, const resolve::ColorScale& scale, const char* title)
{
    const float fontSize = ImGui::GetFontSize();
    const float barHeight = ImGui::GetFrameHeight();
    const float gap = std::round(0.3f * fontSize);
    const float tickLength = std::round(0.3f * fontSize);
    const std::string aboveLabel = scale.aboveLabel();
    const float boxWidth = std::round(std::max(1.6f * fontSize, ImGui::CalcTextSize(aboveLabel.c_str()).x + fontSize));
    const ImU32 outline = ImGui::GetColorU32(ImGuiCol_Border);
    const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);

    draw->AddText(origin, text, title);
    const float top = origin.y + fontSize + gap;
    const float bottom = top + barHeight;
    const float rampLeft = origin.x + boxWidth + gap;
    const float rampRight = origin.x + width - boxWidth - gap;
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

    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mouse = io.MousePos;
    if (!io.WantCaptureMouse && mouse.y >= top && mouse.y <= bottom && mouse.x >= origin.x &&
        mouse.x <= origin.x + width) {
        if (mouse.x >= rampLeft && mouse.x <= rampRight) {
            const float t = (mouse.x - rampLeft) / (rampRight - rampLeft);
            ImGui::SetTooltip("%s: %s", title, resolve::formatCount(scale.countAt(t)).c_str());
        } else {
            ImGui::SetTooltip(mouse.x < rampLeft ? "Count 0" : "Count above the scale maximum");
        }
    }
    return bottom + tickLength + fontSize - origin.y;
}

PixelRect framebufferRect(ImVec2 areaMin, ImVec2 areaMax)
{
    const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
    return {static_cast<std::int32_t>(std::lround(areaMin.x * scale.x)),
            static_cast<std::int32_t>(std::lround(areaMin.y * scale.y)),
            static_cast<std::int32_t>(std::lround(areaMax.x * scale.x)),
            static_cast<std::int32_t>(std::lround(areaMax.y * scale.y))};
}

FrameInWindow::FrameInWindow(std::uint32_t width, std::uint32_t height, ImVec2 areaMin, ImVec2 areaMax)
{
    const ImVec2 framebufferScale = ImGui::GetIO().DisplayFramebufferScale;
    const PixelRect box = letterbox(width, height, framebufferRect(areaMin, areaMax));
    left = static_cast<float>(box.x0) / framebufferScale.x;
    top = static_cast<float>(box.y0) / framebufferScale.y;
    scale = static_cast<float>(box.width()) / framebufferScale.x / static_cast<float>(width);
}

} // namespace thesis::app
