// Shading of the work counters: the work of each ray → image. For diagnosis: it shows where
// rays do much work. The counters come from the counting variant of a tracer (work.slang).

import types;
import display;
#include "thesis/shared/Bindings.h"
#include "thesis/shared/Hit.h"

struct ShadeWorkParams
{
    uint width;
    uint height;
    uint mode;   // 0 cells visited, 1 primitives tested, 2 runs tested, 3 kind of the cell at the hit,
                 // 4 steps through the search structure, 5 mipmap cells read,
                 // 6 the largest mipmap square that the ray skipped
    uint pad0;
    float scale; // the count that gets the last colour of the scale
    float pad1;
    float pad2;
    float pad3;
};

[[vk::push_constant]] ConstantBuffer<ShadeWorkParams> gParams;

[[vk::binding(THESIS_BINDING_SHADE_HITS, 0)]] StructuredBuffer<Hit> gHits;
[[vk::binding(THESIS_BINDING_SHADE_OUTPUT, 0)]] [[vk::image_format("rgba8")]] RWTexture2D<float4> gOutput;
[[vk::binding(THESIS_BINDING_SHADE_WORK, 0)]] StructuredBuffer<RayWork> gWork;

// One colour for each mipmap level 1 to 6 (squares of 2x2 to 64x64 cells). The panel names
// them in the legend of the view (ControlPanel.cpp).
float3 levelColor(uint level)
{
    const float3 colors[6] = {float3(0.20, 0.40, 0.95), float3(0.15, 0.75, 0.30), float3(0.95, 0.85, 0.10),
                              float3(0.95, 0.45, 0.10), float3(0.85, 0.15, 0.15), float3(0.80, 0.30, 0.85)};
    return colors[min(level, 6u) - 1];
}

// Colour scale for a count: black (0), blue, green, yellow, red (`scale`), white (more than `scale`).
float3 heat(float count, float scale)
{
    if (count <= 0.0)
        return float3(0.0, 0.0, 0.0);
    if (count > scale)
        return float3(1.0, 1.0, 1.0);
    const float3 stops[4] = {float3(0.05, 0.15, 0.85), float3(0.10, 0.75, 0.25), float3(0.95, 0.85, 0.10),
                             float3(0.90, 0.10, 0.10)};
    const float x = 3.0 * count / max(scale, 1.0);
    const uint segment = min(uint(x), 2u);
    return lerp(stops[segment], stops[segment + 1], x - float(segment));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 threadId : SV_DispatchThreadID)
{
    if (threadId.x >= gParams.width || threadId.y >= gParams.height)
        return;

    const uint index = threadId.y * gParams.width + threadId.x;
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
            color = float3(0.15, 0.70, 0.20); // green: one voxel in the column
        else if (work.hitCell == THESIS_HIT_CELL_ONE_RUN)
            color = float3(0.15, 0.30, 0.90); // blue: one run of several voxels
        else if (work.hitCell == THESIS_HIT_CELL_SEVERAL_RUNS)
            color = float3(0.90, 0.15, 0.10); // red: more than one run
        else if (gHits[index].primitive != kNoHit)
            color = float3(0.5, 0.5, 0.5);    // a hit on a primitive kind without cells
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
        color = heat(float(count), gParams.scale);
    }

    gOutput[threadId.xy] = toDisplay(color);
}
