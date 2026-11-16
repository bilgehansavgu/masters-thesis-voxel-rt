// Software tracer: our own search structure, a uniform grid, walked in a compute shader.
//
// A uniform grid is a 3D array of equal cells over the scene. Each cell lists the primitives
// whose boxes touch it (built in UniformGrid.cpp). The ray walks the cells front to back with a
// 3D DDA and tests the primitives of each cell. A hit inside the current cell is the closest
// hit, because every later cell is farther away.
//
// Compiled once per primitive kind; `primitive` resolves to shaders/primitives/<kind>/.

import types;
import closest;
#include "thesis/shared/Bindings.h"

struct TraceParams
{
    float3 gridOrigin; // corner of cell (0, 0, 0)
    uint   rayCount;
    float3 cellSize;
    uint   pad0;
    uint3  gridSize;   // cells along each axis
    uint   pad1;
};

[[vk::push_constant]] ConstantBuffer<TraceParams> gParams;

[[vk::binding(THESIS_BINDING_RAYS, 0)]] StructuredBuffer<Ray> gRays;
[[vk::binding(THESIS_BINDING_HITS, 0)]] RWStructuredBuffer<Hit> gHits;
// One entry for each cell and one more: the list of cell c is gGridItems[gGridCells[c] .. gGridCells[c + 1]).
[[vk::binding(THESIS_BINDING_SOFTWARE_GRID_CELLS, 0)]] StructuredBuffer<uint> gGridCells;
// Primitive indices.
[[vk::binding(THESIS_BINDING_SOFTWARE_GRID_ITEMS, 0)]] StructuredBuffer<uint> gGridItems;

// The part of the ray inside the grid: from tEnter to tExit (slab test). False if there is none.
bool clipToGrid(Ray ray, out float tEnter, out float tExit)
{
    const float3 lo = gParams.gridOrigin;
    const float3 hi = lo + gParams.cellSize * float3(gParams.gridSize);
    tEnter = ray.tMin;
    tExit = ray.tMax;
    for (uint axis = 0; axis < 3; ++axis)
    {
        if (ray.direction[axis] == 0.0)
        {
            if (ray.origin[axis] < lo[axis] || ray.origin[axis] > hi[axis])
                return false;
            continue;
        }
        const float tA = (lo[axis] - ray.origin[axis]) / ray.direction[axis];
        const float tB = (hi[axis] - ray.origin[axis]) / ray.direction[axis];
        tEnter = max(tEnter, min(tA, tB));
        tExit = min(tExit, max(tA, tB));
    }
    return tEnter <= tExit;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 threadId : SV_DispatchThreadID)
{
    uint rayIndex = threadId.x;
    if (rayIndex >= gParams.rayCount)
        return;

    Ray ray = gRays[rayIndex];
    ClosestHit closest = ClosestHit.none(ray);

    float tEnter;
    float tExit;
    if (clipToGrid(ray, tEnter, tExit))
    {
        // Set up the walk (Amanatides and Woo 1987).
        const int3 last = int3(gParams.gridSize) - 1;
        const float3 start = (ray.origin + tEnter * ray.direction - gParams.gridOrigin) / gParams.cellSize;
        int3 cell = clamp(int3(floor(start)), int3(0, 0, 0), last);
        int3 step = int3(0, 0, 0);
        float3 tNext = float3(kNever, kNever, kNever);  // where the ray leaves the current cell along each axis
        float3 tDelta = float3(kNever, kNever, kNever); // distance between two cell borders along each axis
        for (uint axis = 0; axis < 3; ++axis)
        {
            if (ray.direction[axis] == 0.0)
                continue;
            step[axis] = ray.direction[axis] > 0.0 ? 1 : -1;
            tDelta[axis] = abs(gParams.cellSize[axis] / ray.direction[axis]);
            const float border =
                gParams.gridOrigin[axis] + float(cell[axis] + (ray.direction[axis] > 0.0 ? 1 : 0)) * gParams.cellSize[axis];
            tNext[axis] = (border - ray.origin[axis]) / ray.direction[axis];
        }

        const uint maxCells = gParams.gridSize.x + gParams.gridSize.y + gParams.gridSize.z;
        for (uint visited = 0; visited < maxCells; ++visited)
        {
            const uint index = uint(cell.x) + gParams.gridSize.x * (uint(cell.y) + gParams.gridSize.y * uint(cell.z));
            const uint end = gGridCells[index + 1];
            for (uint item = gGridCells[index]; item < end; ++item)
                closest.test(ray, gGridItems[item]);

            // A primitive can reach into later cells, and so can its hit. The search is over
            // when the closest hit lies inside the cells visited so far.
            const float tLeave = min(min(tNext.x, tNext.y), tNext.z);
            if (closest.t <= tLeave || tLeave >= tExit)
                break;

            const uint axis = tNext.x <= tNext.y ? (tNext.x <= tNext.z ? 0 : 2) : (tNext.y <= tNext.z ? 1 : 2);
            cell[axis] += step[axis];
            if (cell[axis] < 0 || cell[axis] > last[axis])
                break;
            tNext[axis] += tDelta[axis];
        }
    }

    gHits[rayIndex] = closest.finish();
    closest.work.store(rayIndex);
}
