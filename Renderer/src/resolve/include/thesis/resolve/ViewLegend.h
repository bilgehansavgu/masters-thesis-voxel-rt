#pragma once

// Side: CPU

#include "thesis/resolve/ColorScale.h"
#include "thesis/resolve/ShadingParams.h"

#include <optional>
#include <string>
#include <vector>

namespace thesis::resolve {

// A linear colour (as the shaders compute it) as the image stores it: value^(1 / gamma), the same
// as toDisplay() in display.slang.
[[nodiscard]] DisplayColor toDisplay(float r, float g, float b);

// One colour of a legend and what it means. With `bottom`, the box is a vertical gradient from
// `color` at the top to `bottom`: the background of the image is one.
struct Swatch {
    DisplayColor color;
    std::string label;
    std::optional<DisplayColor> bottom;
};

// A continuous scale: a strip of colours from left to right, with numbered ticks, and optionally
// a box at each end for the values outside the strip.
struct LegendBar {
    std::vector<DisplayColor> ramp; // evenly spaced samples, left to right
    std::vector<ScaleTick> ticks;
    std::optional<Swatch> below; // left of the strip
    std::optional<Swatch> above; // right of the strip
    ColorScale countScale;       // count bars: the scale, for the value under the mouse
    bool isCount = false;
    float rangeStart = 0.0f; // other bars: the value at the left and the right end of the strip
    float rangeEnd = 1.0f;

    // The value at position t in [0, 1] along the strip, as text.
    [[nodiscard]] std::string valueAt(float t) const;
};

// What the colours of one view mean: a title, then a bar, a row of swatches and a note, each
// where the view needs it. Drawn in the window over the image and below a --screenshot image.
struct ViewLegend {
    std::string title;
    std::optional<LegendBar> bar;
    std::vector<Swatch> swatches;
    std::string note;

    // True for a view whose colours carry no data (the lit views): it gets no legend.
    [[nodiscard]] bool empty() const { return title.empty(); }
};

// The legend of the view `params.mode`, with its current range and scale.
[[nodiscard]] ViewLegend legendFor(const ShadingParams& params);

} // namespace thesis::resolve
