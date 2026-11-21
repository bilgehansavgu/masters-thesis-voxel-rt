#pragma once

// Side: View

#include "Letterbox.h"

#include "thesis/resolve/ColorScale.h"
#include "thesis/resolve/ShadingParams.h"

#include <imgui.h>

#include <cstdint>
#include <span>
#include <string>

namespace thesis::app {

// One view of the image: its key, its name in the panel, and what its colours mean.
struct ViewInfo {
    resolve::ShadingMode mode;
    ImGuiKey key;
    const char* keyName;
    const char* name;
    const char* legend;
};

// The views of the hit (1 to 5) and of the work of each ray (6 to 0, -, =), in panel order.
[[nodiscard]] std::span<const ViewInfo> imageViews();
[[nodiscard]] std::span<const ViewInfo> workViews();
[[nodiscard]] const ViewInfo* findView(resolve::ShadingMode mode);

// The text colour of a problem the user must act on.
inline constexpr ImVec4 kProblemColor{1.0f, 0.45f, 0.35f, 1.0f};
// The text colour of the one thing to look at: the hit, a positive step of the ray walk.
inline constexpr ImVec4 kHighlightColor{0.95f, 0.78f, 0.32f, 1.0f};

// "12.3 MiB" or "4.5 KiB".
[[nodiscard]] std::string formatBytes(std::uint64_t bytes);
// Text that breaks at the right side of the window; `disabled`: in the muted colour.
void wrapped(const std::string& text, bool disabled = false);
// A muted "(?)" on the same line that shows `text` when the mouse is over it.
void helpMarker(const char* text);
// A collapsing section header in grey: the accent colour stays for what is selected.
bool sectionHeader(const char* title, bool openByDefault = true);
// A label in a fixed-width column, so the controls after it line up: "Layout   (o) inline ( ) split".
void rowLabel(const char* label);

// The colour bar of a count view: a black box for 0, the colour map with ticks, a white box for
// counts above the maximum. The colours are the ones of the image (resolve::ColorScale), the same
// bar that --screenshot puts below the image. Hovering the map shows the count at that colour.
// Drawn into `draw` from `origin` (top left, the title first) with `width`; returns the height it
// used. When the mouse is over the bar (and not over a window), a tooltip shows the count there.
float drawColorBar(ImDrawList* draw, ImVec2 origin, float width, const resolve::ColorScale& scale, const char* title);

// Where the frame is in the window: pixel (x, y) of the frame covers the window points from
// (left + x * scale, top + y * scale), in ImGui coordinates. `area` is the image area of the
// window in ImGui coordinates; the frame is letterboxed in it as the presenter does.
struct FrameInWindow {
    float left = 0.0f;
    float top = 0.0f;
    float scale = 1.0f; // window points for each pixel of the frame

    FrameInWindow(std::uint32_t width, std::uint32_t height, ImVec2 areaMin, ImVec2 areaMax);
};

// The image area in window pixels (framebuffer), as the presenter needs it.
[[nodiscard]] PixelRect framebufferRect(ImVec2 areaMin, ImVec2 areaMax);

} // namespace thesis::app
