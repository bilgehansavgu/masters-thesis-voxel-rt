#include "thesis/resolve/LegendPainter.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <sstream>
#include <vector>

namespace thesis::resolve {
namespace {

constexpr DisplayColor kTextColor{0.92f, 0.92f, 0.92f};
constexpr DisplayColor kNoteColor{0.70f, 0.72f, 0.75f};
constexpr DisplayColor kOutlineColor{0.6f, 0.6f, 0.6f};
constexpr DisplayColor kDarkInk{0.0f, 0.0f, 0.0f};
constexpr DisplayColor kLightInk{1.0f, 1.0f, 1.0f};

// Text on a box: dark on a light fill, light on a dark one.
DisplayColor inkFor(DisplayColor fill)
{
    return 0.2126f * fill.r + 0.7152f * fill.g + 0.0722f * fill.b > 0.5f ? kDarkInk : kLightInk;
}

// Splits `text` at spaces into lines no wider than `width`. A single word wider than that gets a
// line of its own.
std::vector<std::string> wrapLines(const ILegendPainter& painter, const std::string& text, float width)
{
    std::vector<std::string> lines;
    std::istringstream words(text);
    std::string word;
    std::string line;
    while (words >> word) {
        const std::string longer = line.empty() ? word : line + " " + word;
        if (!line.empty() && painter.textWidth(longer) > width) {
            lines.push_back(line);
            line = word;
        } else {
            line = longer;
        }
    }
    if (!line.empty()) {
        lines.push_back(line);
    }
    return lines;
}

// Paints the lines of `text`, each centred in the column, and returns the y below them.
float centredText(ILegendPainter& painter, const std::string& text, float left, float y, float width,
                  DisplayColor color)
{
    for (const std::string& line : wrapLines(painter, text, width)) {
        painter.text(std::round(left + 0.5f * (width - painter.textWidth(line))), y, line, color);
        y += painter.fontSize();
    }
    return y;
}

// A filled box with an outline: flat, or a vertical gradient for a swatch with `bottom`.
void box(ILegendPainter& painter, float x0, float y0, float x1, float y1, const Swatch& swatch)
{
    painter.fillVertical(x0, y0, x1, y1, swatch.color, swatch.bottom.value_or(swatch.color));
    painter.outline(x0, y0, x1, y1, kOutlineColor);
}

// Paints the bar and its ticks from `y`; returns the y below the tick labels.
float paintBar(ILegendPainter& painter, const LegendBar& bar, float left, float y, float width, LegendLayout& layout)
{
    const float s = painter.fontSize();
    const float gap = std::round(0.3f * s);
    const float barHeight = std::round(1.2f * s);
    const float tickLength = std::round(0.3f * s);
    const auto boxWidth = [&](const std::optional<Swatch>& end) {
        return end ? std::round(std::max(1.6f * s, painter.textWidth(end->label) + 0.8f * s)) : 0.0f;
    };
    const float belowWidth = boxWidth(bar.below);
    const float aboveWidth = boxWidth(bar.above);
    const float top = std::round(y);
    const float bottom = top + barHeight;
    const float rampLeft = std::round(left + (bar.below ? belowWidth + gap : 0.0f));
    // In a column narrower than the end boxes the strip has no width; it never turns negative.
    const float rampRight = std::max(rampLeft, std::round(left + width - (bar.above ? aboveWidth + gap : 0.0f)));
    const float textInBox = top + std::round(0.5f * (barHeight - s));

    const auto endBox = [&](const Swatch& swatch, float x0, float boxW) {
        box(painter, x0, top, x0 + boxW, bottom, swatch);
        painter.text(std::round(x0 + 0.5f * (boxW - painter.textWidth(swatch.label))), textInBox, swatch.label,
                     inkFor(swatch.color));
    };
    if (bar.below) {
        endBox(*bar.below, std::round(left), belowWidth);
    }
    if (bar.above) {
        endBox(*bar.above, std::round(left + width - aboveWidth), aboveWidth);
    }

    const std::size_t samples = bar.ramp.size();
    const float rampWidth = rampRight - rampLeft;
    for (std::size_t i = 0; i + 1 < samples; ++i) {
        const float x0 = rampLeft + rampWidth * static_cast<float>(i) / static_cast<float>(samples - 1);
        const float x1 = rampLeft + rampWidth * static_cast<float>(i + 1) / static_cast<float>(samples - 1);
        painter.fillHorizontal(x0, top, x1, bottom, bar.ramp[i], bar.ramp[i + 1]);
    }
    painter.outline(rampLeft, top, rampRight, bottom, kOutlineColor);
    layout.hasRamp = true;
    layout.rampLeft = rampLeft;
    layout.rampRight = rampRight;
    layout.rampTop = top;
    layout.rampBottom = bottom;

    // Ticks below the strip. A label that would overlap the one before it is left out.
    float previousLabelRight = -FLT_MAX;
    for (const ScaleTick& tick : bar.ticks) {
        const float x = std::round(rampLeft + tick.position * rampWidth);
        painter.fillVertical(x, bottom, x + 1.0f, bottom + tickLength, kTextColor, kTextColor);
        const float labelWidth = painter.textWidth(tick.label);
        // A label wider than the strip starts at its left end (std::clamp needs low <= high).
        const float labelLeft = std::clamp(x - 0.5f * labelWidth, rampLeft, std::max(rampLeft, rampRight - labelWidth));
        if (labelLeft < previousLabelRight + 0.5f * s) {
            continue;
        }
        painter.text(std::round(labelLeft), bottom + tickLength, tick.label, kTextColor);
        previousLabelRight = labelLeft + labelWidth;
    }
    return bottom + tickLength + s;
}

// Paints the swatches in rows that wrap at `width`, each row centred; returns the y below them.
float paintSwatches(ILegendPainter& painter, const std::vector<Swatch>& swatches, float left, float y, float width)
{
    const float s = painter.fontSize();
    const float gap = std::round(0.3f * s);
    const float boxSize = std::round(0.9f * s);
    const float spacing = std::round(1.2f * s);
    const auto itemWidth = [&](const Swatch& swatch) { return boxSize + gap + painter.textWidth(swatch.label); };

    std::size_t first = 0;
    while (first < swatches.size()) {
        // The items of this row: as many as fit, at least one.
        std::size_t end = first + 1;
        float rowWidth = itemWidth(swatches[first]);
        while (end < swatches.size() && rowWidth + spacing + itemWidth(swatches[end]) <= width) {
            rowWidth += spacing + itemWidth(swatches[end]);
            ++end;
        }
        float x = std::round(left + 0.5f * (width - rowWidth));
        const float boxTop = std::round(y + 0.5f * (s - boxSize));
        for (std::size_t i = first; i < end; ++i) {
            box(painter, x, boxTop, x + boxSize, boxTop + boxSize, swatches[i]);
            painter.text(x + boxSize + gap, y, swatches[i].label, kTextColor);
            x += std::round(itemWidth(swatches[i]) + spacing);
        }
        y += s + gap;
        first = end;
    }
    return y;
}

// Measures without painting: it only forwards the font metrics.
class MeasuringPainter final : public ILegendPainter {
public:
    explicit MeasuringPainter(const ILegendPainter& metrics) : metrics_(metrics) {}

    [[nodiscard]] float fontSize() const override { return metrics_.fontSize(); }
    [[nodiscard]] float textWidth(const std::string& text) const override { return metrics_.textWidth(text); }
    void fillVertical(float, float, float, float, DisplayColor, DisplayColor) override {}
    void fillHorizontal(float, float, float, float, DisplayColor, DisplayColor) override {}
    void outline(float, float, float, float, DisplayColor) override {}
    void text(float, float, const std::string&, DisplayColor) override {}

private:
    const ILegendPainter& metrics_;
};

} // namespace

LegendLayout paintLegend(ILegendPainter& painter, const ViewLegend& legend, float left, float top, float width)
{
    LegendLayout layout;
    if (legend.empty()) {
        return layout;
    }
    const float block = std::round(0.5f * painter.fontSize());
    float y = centredText(painter, legend.title, left, top, width, kTextColor);
    if (legend.bar) {
        y = paintBar(painter, *legend.bar, left, y + block, width, layout);
    }
    if (!legend.swatches.empty()) {
        y = paintSwatches(painter, legend.swatches, left, y + block, width);
    }
    if (!legend.note.empty()) {
        y = centredText(painter, legend.note, left, y + block, width, kNoteColor);
    }
    layout.height = y - top;
    return layout;
}

float legendHeight(const ILegendPainter& painter, const ViewLegend& legend, float width)
{
    MeasuringPainter measuring(painter);
    return paintLegend(measuring, legend, 0.0f, 0.0f, width).height;
}

} // namespace thesis::resolve
