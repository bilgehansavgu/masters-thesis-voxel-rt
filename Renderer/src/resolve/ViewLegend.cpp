#include "thesis/resolve/ViewLegend.h"

#include "thesis/shared/ViewColors.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace thesis::resolve {
namespace {

constexpr std::size_t kRampSamples = 256;

using Linear = std::array<double, 3>;

DisplayColor shown(const Linear& color)
{
    return toDisplay(static_cast<float>(color[0]), static_cast<float>(color[1]), static_cast<float>(color[2]));
}

// Where a ray hits nothing: the background gradient of display.slang.
Swatch noHit()
{
    return {shown({THESIS_COLOR_SKY_TOP}), "No hit", shown({THESIS_COLOR_SKY_BOTTOM})};
}

std::string formatValue(double value)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.3g", value);
    return text;
}

ViewLegend countLegend(const ShadingParams& params)
{
    const ColorScale& scale = params.workScale;
    LegendBar bar;
    bar.ramp.reserve(kRampSamples);
    for (std::size_t i = 0; i < kRampSamples; ++i) {
        bar.ramp.push_back(viridis(static_cast<float>(i) / static_cast<float>(kRampSamples - 1)));
    }
    bar.ticks = scale.ticks();
    bar.below = Swatch{kZeroCountColor, "0", std::nullopt};
    bar.above = Swatch{kAboveScaleColor, scale.aboveLabel(), std::nullopt};
    bar.countScale = scale;
    bar.isCount = true;
    std::string title = countedQuantity(params.mode);
    if (params.warpSize > 0)
        title += ", largest in the warp of " + std::to_string(params.warpSize) + " rays";
    return {title + " (" + scale.kindName() + " scale)", bar, {}, {}};
}

// Same formula as mode 1 of shade.slang: grey 1 - d, with d = (t - near) / (far - near) in [0, 1].
ViewLegend distanceLegend(const ShadingParams& params)
{
    LegendBar bar;
    bar.ramp.reserve(kRampSamples);
    for (std::size_t i = 0; i < kRampSamples; ++i) {
        const float grey = 1.0f - static_cast<float>(i) / static_cast<float>(kRampSamples - 1);
        bar.ramp.push_back(toDisplay(grey, grey, grey));
    }
    bar.rangeStart = params.distanceNear;
    bar.rangeEnd = params.distanceFar;
    bar.ticks = roundTicks(params.distanceNear, params.distanceFar, false);
    return {"Hit distance along the ray (scene units)",
            bar,
            {noHit()},
            "Nearer than the start of the range: white. Farther than its end: black."};
}

ViewLegend hitCellLegend()
{
    return {"Kind of the primary grid cell at the hit (Reichl et al. 2012, fig. 7)",
            std::nullopt,
            {{shown({THESIS_COLOR_HIT_HEIGHT}), "Height cell: one voxel", std::nullopt},
             {shown({THESIS_COLOR_HIT_ONE_RUN}), "One run", std::nullopt},
             {shown({THESIS_COLOR_HIT_SEVERAL_RUNS}), "Several runs", std::nullopt},
             {shown({THESIS_COLOR_HIT_NO_CELLS}), "Primitive without cells", std::nullopt},
             noHit()},
            {}};
}

ViewLegend skipLevelLegend()
{
    constexpr std::array<Linear, THESIS_MIPMAP_LEVELS> levels{{{THESIS_COLOR_LEVEL_1},
                                                               {THESIS_COLOR_LEVEL_2},
                                                               {THESIS_COLOR_LEVEL_3},
                                                               {THESIS_COLOR_LEVEL_4},
                                                               {THESIS_COLOR_LEVEL_5},
                                                               {THESIS_COLOR_LEVEL_6}}};
    std::vector<Swatch> swatches;
    for (std::size_t level = 1; level <= levels.size(); ++level) {
        const std::string side = std::to_string(1u << level);
        swatches.push_back({shown(levels[level - 1]), side + "x" + side, std::nullopt});
    }
    swatches.push_back({kZeroCountColor, "None skipped", std::nullopt});
    return {"Largest min/max mipmap square the ray skipped (cells)", std::nullopt, swatches,
            "Black also for an encoding without mipmap."};
}

} // namespace

DisplayColor toDisplay(float r, float g, float b)
{
    const auto encode = [](float value) {
        return static_cast<float>(
            std::pow(std::clamp(static_cast<double>(value), 0.0, 1.0), 1.0 / THESIS_DISPLAY_GAMMA));
    };
    return {encode(r), encode(g), encode(b)};
}

std::string LegendBar::valueAt(float t) const
{
    if (isCount) {
        return formatCount(countScale.countAt(t));
    }
    return formatValue(rangeStart + std::clamp(t, 0.0f, 1.0f) * (rangeEnd - rangeStart));
}

ViewLegend legendFor(const ShadingParams& params)
{
    if (showsCount(params.mode)) {
        return countLegend(params);
    }
    switch (params.mode) {
    case ShadingMode::Distance:
        return distanceLegend(params);
    case ShadingMode::WorkHitCell:
        return hitCellLegend();
    case ShadingMode::WorkSkipLevel:
        return skipLevelLegend();
    default:
        // The lit views show light only. The primitive id and the stored normal are read as
        // pictures (where bricks are, how the surface turns), not as values: the panel text
        // describes them.
        return {};
    }
}

} // namespace thesis::resolve
