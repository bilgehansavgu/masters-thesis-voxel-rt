#include "PanelWidgets.h"

#include "thesis/resolve/LegendPainter.h"

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
     "Kind of the primary grid cell with the hit, as in figure 7 of Reichl et al. 2012. The legend on the image "
     "names the colours."},
    {ShadingMode::WorkStructure, ImGuiKey_0, "0", "Search structure steps",
     "Cells or nodes of the tracer's own search structure that the ray read. The hardware tracer cannot count them."},
    {ShadingMode::WorkMipmap, ImGuiKey_Minus, "-", "Mipmap cells read",
     "Cells of the min/max mipmaps that the ray read. Zero for an encoding without mipmap."},
    {ShadingMode::WorkSkipLevel, ImGuiKey_Equal, "=", "Largest mipmap square skipped",
     "The largest square of the min/max mipmap that the walk skipped for this ray, because the ray passes above or "
     "below every voxel in it. The legend on the image names the colours."},
    {ShadingMode::WorkCycles, ImGuiKey_C, "C", "GPU clock cycles",
     "Cycles from the start to the end of the ray, read with the clock of its warp. A ray that finishes early waits "
     "for the slowest ray of its warp, so this is the cost the GPU pays for it. Ray queries have it too; a ray "
     "tracing pipeline does not: after TraceRay the GPU may run the ray on another SM, whose clock differs. "
     "Measured in the counting shader: for looking, never a thesis number."},
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

namespace {

// Paints a legend into an ImGui draw list; resolve::paintLegend() gives the layout, the same as in
// screenshots. Without a draw list it only measures.
class DrawListPainter final : public resolve::ILegendPainter {
public:
    explicit DrawListPainter(ImDrawList* draw) : draw_(draw) {}

    [[nodiscard]] float fontSize() const override { return ImGui::GetFontSize(); }
    [[nodiscard]] float textWidth(const std::string& text) const override
    {
        return ImGui::CalcTextSize(text.c_str()).x;
    }
    void fillVertical(float x0, float y0, float x1, float y1, resolve::DisplayColor top,
                      resolve::DisplayColor bottom) override
    {
        if (draw_) {
            const ImU32 t = imguiColor(top), b = imguiColor(bottom);
            draw_->AddRectFilledMultiColor({x0, y0}, {x1, y1}, t, t, b, b);
        }
    }
    void fillHorizontal(float x0, float y0, float x1, float y1, resolve::DisplayColor left,
                        resolve::DisplayColor right) override
    {
        if (draw_) {
            const ImU32 l = imguiColor(left), r = imguiColor(right);
            draw_->AddRectFilledMultiColor({x0, y0}, {x1, y1}, l, r, r, l);
        }
    }
    void outline(float x0, float y0, float x1, float y1, resolve::DisplayColor color) override
    {
        if (draw_) {
            draw_->AddRect({x0 - 0.5f, y0 - 0.5f}, {x1 + 0.5f, y1 + 0.5f}, imguiColor(color));
        }
    }
    void text(float x, float y, const std::string& text, resolve::DisplayColor color) override
    {
        if (draw_) {
            draw_->AddText({x, y}, imguiColor(color), text.c_str());
        }
    }

private:
    ImDrawList* draw_;
};

} // namespace

float legendHeight(const resolve::ViewLegend& legend, float width)
{
    const DrawListPainter metrics(nullptr);
    return resolve::legendHeight(metrics, legend, width);
}

float drawLegend(ImDrawList* draw, ImVec2 origin, float width, const resolve::ViewLegend& legend)
{
    DrawListPainter painter(draw);
    const resolve::LegendLayout layout = resolve::paintLegend(painter, legend, origin.x, origin.y, width);

    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mouse = io.MousePos;
    if (layout.hasRamp && !io.WantCaptureMouse && mouse.y >= layout.rampTop && mouse.y <= layout.rampBottom &&
        mouse.x >= layout.rampLeft && mouse.x <= layout.rampRight) {
        const float t = (mouse.x - layout.rampLeft) / (layout.rampRight - layout.rampLeft);
        ImGui::SetTooltip("%s", legend.bar->valueAt(t).c_str());
    }
    return layout.height;
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
