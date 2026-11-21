// Shading of the work counters: the work of each ray → image. For diagnosis: it shows where
// rays do much work. The counters come from the counting variant of a tracer (work.slang).

import types;
import display;
#include "thesis/shared/Bindings.h"
#include "thesis/shared/ColorScale.h"
#include "thesis/shared/Hit.h"
#include "thesis/shared/ViewColors.h"
#include "thesis/shared/RayOrder.h"

struct ShadeWorkParams
{
    uint width;
    uint height;
    uint mode;   // 0 cells visited, 1 primitives tested, 2 runs tested, 3 kind of the cell at the hit,
                 // 4 steps through the search structure, 5 mipmap cells read,
                 // 6 the largest mipmap square that the ray skipped
    uint scaleKind; // THESIS_COLOR_SCALE_LINEAR or THESIS_COLOR_SCALE_LOG2
    float scale;    // the count at the end of the colour map; a larger count is white
    uint rayOrder;  // THESIS_RAY_ORDER_*: where the hit and work of a pixel are in their lists
    float pad2;
    float pad3;
};

[[vk::push_constant]] ConstantBuffer<ShadeWorkParams> gParams;

[[vk::binding(THESIS_BINDING_SHADE_HITS, 0)]] StructuredBuffer<Hit> gHits;
[[vk::binding(THESIS_BINDING_SHADE_OUTPUT, 0)]] [[vk::image_format("rgba8")]] RWTexture2D<float4> gOutput;
[[vk::binding(THESIS_BINDING_SHADE_WORK, 0)]] StructuredBuffer<RayWork> gWork;

// One colour for each mipmap level 1 to 6 (squares of 2x2 to 64x64 cells), from ViewColors.h.
// resolve/ViewLegend.cpp names them in the legend of the view.
float3 levelColor(uint level)
{
    const float3 colors[THESIS_MIPMAP_LEVELS] = {float3(THESIS_COLOR_LEVEL_1), float3(THESIS_COLOR_LEVEL_2),
                                                 float3(THESIS_COLOR_LEVEL_3), float3(THESIS_COLOR_LEVEL_4),
                                                 float3(THESIS_COLOR_LEVEL_5), float3(THESIS_COLOR_LEVEL_6)};
    return colors[min(level, uint(THESIS_MIPMAP_LEVELS)) - 1];
}

// Viridis at t in [0, 1], display-encoded (sRGB). Same formula as viridis() in resolve/ColorScale.cpp.
float3 viridis(float t)
{
    const float3 c[7] = {float3(THESIS_VIRIDIS_C0), float3(THESIS_VIRIDIS_C1), float3(THESIS_VIRIDIS_C2),
                         float3(THESIS_VIRIDIS_C3), float3(THESIS_VIRIDIS_C4), float3(THESIS_VIRIDIS_C5),
                         float3(THESIS_VIRIDIS_C6)};
    const float x = saturate(t);
    float3 value = c[6];
    for (int k = 5; k >= 0; --k)
        value = c[k] + x * value;
    return saturate(value);
}

// Position of a count in (0, scale] along the map. Same formula as ColorScale::position() in
// resolve/ColorScale.cpp, which also draws the colour bar.
float scalePosition(float count, float scale, uint kind)
{
    if (kind == THESIS_COLOR_SCALE_LOG2)
        return saturate(log2(max(count, 1.0)) / log2(max(scale, 2.0)));
    return saturate(count / max(scale, 1.0));
}

// Colour of a count, linear like every colour that goes through toDisplay(): black for 0, the map
// up to `scale`, white above it.
float3 countColor(float count, float scale, uint kind)
{
    if (count <= 0.0)
        return float3(0.0, 0.0, 0.0);
    if (count > scale)
        return float3(1.0, 1.0, 1.0);
    // WHY pow: the map is display-encoded and toDisplay() encodes once more. Decoding first makes
    // the stored value the viridis value, so the image matches the colour bar.
    return pow(viridis(scalePosition(count, scale, kind)), THESIS_DISPLAY_GAMMA);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 threadId : SV_DispatchThreadID)
{
    if (threadId.x >= gParams.width || threadId.y >= gParams.height)
        return;

    const uint index = THESIS_RAY_INDEX(gParams.rayOrder, threadId.x, threadId.y, gParams.width, gParams.height);
    const RayWork work = gWork[index];
    float3 color;

    if (gParams.mode == 6)
    {
        // Black: the walk skipped no square, or the encoding has no mipmap.
        color = work.skippedLevels == 0 ? float3(0.0, 0.0, 0.0) : levelColor(uint(firstbithigh(work.skippedLevels)));
    }
    else if (gParams.mode == 3)
    {
        // The colours of figure 7 in Reichl et al. 2012.
        if (work.hitCell == THESIS_HIT_CELL_HEIGHT)
            color = float3(THESIS_COLOR_HIT_HEIGHT);
        else if (work.hitCell == THESIS_HIT_CELL_ONE_RUN)
            color = float3(THESIS_COLOR_HIT_ONE_RUN);
        else if (work.hitCell == THESIS_HIT_CELL_SEVERAL_RUNS)
            color = float3(THESIS_COLOR_HIT_SEVERAL_RUNS);
        else if (gHits[index].primitive != kNoHit)
            color = float3(THESIS_COLOR_HIT_NO_CELLS);
        else
            color = backgroundColor(float(threadId.y) / float(gParams.height));
    }
    else
    {
        uint count = work.runs;
        if (gParams.mode == 0)
            count = work.cells;
        else if (gParams.mode == 1)
            count = work.primitiveTests;
        else if (gParams.mode == 4)
            count = work.structureSteps;
        else if (gParams.mode == 5)
            count = work.mipmapCells;
        color = countColor(float(count), gParams.scale, gParams.scaleKind);
    }

    gOutput[threadId.xy] = toDisplay(color);
}
