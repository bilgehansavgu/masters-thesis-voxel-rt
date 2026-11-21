#pragma once

// Side: CPU

#include "thesis/shared/ColorScale.h"

#include <cstdint>
#include <string>
#include <vector>

namespace thesis::resolve {

// How a count becomes a position along the colour scale.
enum class ScaleKind : std::uint32_t {
    Linear = THESIS_COLOR_SCALE_LINEAR,
    Log2 = THESIS_COLOR_SCALE_LOG2,
};

// A display-encoded (sRGB) colour, each channel in [0, 1].
struct DisplayColor {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

// The viridis colour at position t in [0, 1] (shared/ColorScale.h). Same formula as viridis() in
// shade_work.slang.
[[nodiscard]] DisplayColor viridis(float t);

// The two colours outside the map. A count of 0 is black: the ray did no such work (a ray that
// misses the scene also does none). A count above the maximum is white: the scale cuts it off.
inline constexpr DisplayColor kZeroCountColor{0.0f, 0.0f, 0.0f};
inline constexpr DisplayColor kAboveScaleColor{1.0f, 1.0f, 1.0f};

// One labelled mark on the colour bar.
struct ScaleTick {
    double value = 0.0;    // the count
    float position = 0.0f; // where along the bar, 0 (start) to 1 (end)
    std::string label;
};

// The mapping of a work count to a colour, and the ticks that a colour bar of it shows.
// The image (shade_work.slang) and every colour bar (panel, screenshot) use this one mapping.
struct ColorScale {
    float maximum = 64.0f; // the count at the end of the map; a larger count is white
    ScaleKind kind = ScaleKind::Linear;

    // Position along the map of a count in (0, maximum]: count / maximum, or for the log scale
    // log2(count) / log2(maximum). Same formula as scalePosition() in shade_work.slang.
    [[nodiscard]] float position(double count) const;

    // The count at position t in [0, 1]: the inverse of position().
    [[nodiscard]] double countAt(float t) const;

    // The colour of a count, as in the image: black for 0, white above the maximum.
    [[nodiscard]] DisplayColor color(double count) const;

    // The ticks of the map, all in (0, maximum]; 0 has its own black box on a bar. Linear: multiples
    // of 1, 2 or 5 times a power of ten, at most 5 of them. Log: powers of two from 1, every second
    // one when there would be more than 7.
    [[nodiscard]] std::vector<ScaleTick> ticks() const;

    // Label of the box at the end of the bar for counts above the maximum, for example "> 64".
    [[nodiscard]] std::string aboveLabel() const;

    // "linear" or "log2", for titles and printed output.
    [[nodiscard]] const char* kindName() const;
};

// Text of a count on the scale: an integer without decimals, else up to 3 significant digits.
[[nodiscard]] std::string formatCount(double value);

} // namespace thesis::resolve
