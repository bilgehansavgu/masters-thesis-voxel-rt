#pragma once

// Side: CPU

#include "thesis/resolve/ViewLegend.h"

#include <string>

namespace thesis::resolve {

// What the legend layout needs from a surface: font size, text width and a few filled shapes.
// The window paints into an ImGui draw list, a screenshot into its pixels. Both use paintLegend(),
// so the legend has one layout.
class ILegendPainter {
public:
    virtual ~ILegendPainter() = default;

    [[nodiscard]] virtual float fontSize() const = 0;
    [[nodiscard]] virtual float textWidth(const std::string& text) const = 0;
    // A rectangle that goes from `top` colour at its top edge to `bottom` at its bottom edge.
    virtual void fillVertical(float x0, float y0, float x1, float y1, DisplayColor top, DisplayColor bottom) = 0;
    // A rectangle that goes from `left` colour at its left edge to `right` at its right edge.
    virtual void fillHorizontal(float x0, float y0, float x1, float y1, DisplayColor left, DisplayColor right) = 0;
    // A one-pixel frame just outside the rectangle.
    virtual void outline(float x0, float y0, float x1, float y1, DisplayColor color) = 0;
    // `text` with its top left corner at (x, y).
    virtual void text(float x, float y, const std::string& text, DisplayColor color) = 0;
};

// Where paintLegend() put things: its height, and the colour strip of the bar (for the value
// under the mouse).
struct LegendLayout {
    float height = 0.0f;
    bool hasRamp = false;
    float rampLeft = 0.0f;
    float rampRight = 0.0f;
    float rampTop = 0.0f;
    float rampBottom = 0.0f;
};

// Paints `legend` into the column [left, left + width] from `top` down: the title, the bar with
// its ticks and end boxes, the swatches (in rows that wrap), the note (wrapped). Each line is
// centred in the column. Light text, for a dark background.
LegendLayout paintLegend(ILegendPainter& painter, const ViewLegend& legend, float left, float top, float width);

// The height paintLegend() would use, without painting.
[[nodiscard]] float legendHeight(const ILegendPainter& painter, const ViewLegend& legend, float width);

} // namespace thesis::resolve
