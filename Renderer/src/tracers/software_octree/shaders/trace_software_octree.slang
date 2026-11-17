// Octree tracer: our own search structure, a sparse octree, walked in a compute shader.
//
// The octree stands on a cube of 2^depth cells along each side (Octree.h, thesis/shared/Octree.h).
// A part of space without a primitive is one EMPTY word, however large it is. The ray goes
// from node to node, front to back:
//
//   1. Go down from a node to the smallest node that holds the current cell.
//   2. If that node is one cell with primitives, test them.
//   3. Find where the ray leaves the cube of that node, and the cell on the other side.
//   4. Go on from the lowest node that holds both the old and the new cell.
//
// Step 4 needs the nodes on the way down from the root. The shader keeps one tile number for
// each level, at most 10. The walk in Reichl et al. 2012 keeps nothing and starts at the root
// each time, which costs more node reads.
//
// Compiled once per primitive kind; `primitive` resolves to shaders/primitives/<kind>/.

import types;
import aabb;
import closest;
#include "thesis/shared/Bindings.h"
#include "thesis/shared/RayLog.h"
#include "thesis/shared/Octree.h"

struct TraceParams
{
    float3 origin;   // corner of cell (0, 0, 0)
    uint   rayCount;
    float3 cellSize;
    uint   depth;    // the cube has 2^depth cells along each side
};

[[vk::push_constant]] ConstantBuffer<TraceParams> gParams;

[[vk::binding(THESIS_BINDING_RAYS, 0)]] StructuredBuffer<Ray> gRays;
[[vk::binding(THESIS_BINDING_HITS, 0)]] RWStructuredBuffer<Hit> gHits;
// Tiles of 8 words: the children of one node.
[[vk::binding(THESIS_BINDING_OCTREE_NODES, 0)]] StructuredBuffer<uint> gOctreeNodes;
// The lists of the cells with more than one primitive.
[[vk::binding(THESIS_BINDING_OCTREE_ITEMS, 0)]] StructuredBuffer<uint> gOctreeItems;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 threadId : SV_DispatchThreadID)
{
    uint rayIndex = threadId.x;
    if (rayIndex >= gParams.rayCount)
        return;

    Ray ray = gRays[rayIndex];
    ClosestHit closest = ClosestHit.none(ray, rayIndex);

    const int cells = 1 << gParams.depth;
    float tEnter;
    float tExit;
    if (clipRayToBox(ray, gParams.origin, gParams.origin + gParams.cellSize * float(cells), tEnter, tExit))
    {
        const float3 start = (ray.origin + tEnter * ray.direction - gParams.origin) / gParams.cellSize;
        int3 cell = clamp(int3(floor(start)), int3(0, 0, 0), int3(cells - 1, cells - 1, cells - 1));

        uint tiles[THESIS_OCTREE_MAX_DEPTH]; // tiles[L]: the tile whose children cover 2^L cells, on the way to `cell`
        uint tile = 0;
        uint level = gParams.depth - 1;

        const uint maxNodes = 3u << gParams.depth; // more than any ray can cross
        for (uint visited = 0; visited < maxNodes; ++visited)
        {
            // 1. Go down to the smallest node that holds the cell.
            uint kind = THESIS_OCTREE_EMPTY;
            uint payload = 0;
            for (;;)
            {
                closest.work.structureStep(THESIS_LOG_OCTREE_NODE, uint(cell.x), uint(cell.y), uint(cell.z), level);
                tiles[level] = tile;
                const uint child = ((uint(cell.x) >> level) & 1) | (((uint(cell.y) >> level) & 1) << 1) |
                                   (((uint(cell.z) >> level) & 1) << 2);
                const uint word = gOctreeNodes[tile * 8 + child];
                kind = word >> THESIS_OCTREE_KIND_SHIFT;
                payload = word & THESIS_OCTREE_PAYLOAD_MASK;
                if (kind != THESIS_OCTREE_INTERIOR || level == 0)
                    break;
                tile = payload;
                level -= 1;
            }

            // 2. Test the primitives of the node.
            if (kind == THESIS_OCTREE_SINGLE)
            {
                closest.test(ray, payload);
            }
            else if (kind == THESIS_OCTREE_LIST)
            {
                for (uint item = payload;; ++item)
                {
                    const uint entry = gOctreeItems[item];
                    closest.test(ray, entry & THESIS_OCTREE_ITEM_MASK);
                    if ((entry & THESIS_OCTREE_ITEM_LAST) != 0)
                        break;
                }
            }

            // 3. Where the ray leaves the cube of the node: the nearest of its three far sides.
            const int size = 1 << level;
            const int3 nodeMin = cell & ~(size - 1);
            const float3 lo = gParams.origin + float3(nodeMin) * gParams.cellSize;
            const float3 hi = lo + float(size) * gParams.cellSize;
            float tLeave = kNever;
            uint leaveAxis = 0;
            for (uint axis = 0; axis < 3; ++axis)
            {
                if (ray.direction[axis] == 0.0)
                    continue;
                const float tSide = ((ray.direction[axis] > 0.0 ? hi[axis] : lo[axis]) - ray.origin[axis]) / ray.direction[axis];
                if (tSide < tLeave)
                {
                    tLeave = tSide;
                    leaveAxis = axis;
                }
            }
            // A primitive can reach into later nodes, and so can its hit. The search is over
            // when the closest hit lies inside the nodes visited so far.
            if (closest.t <= tLeave || tLeave >= tExit)
                break;

            // The cell on the other side. Along the axis of the side that the ray crossed, it
            // is the neighbor of the cube, as a whole number. Rounding cannot keep the ray in
            // the same node. Along the other two axes the ray is still inside the cube.
            const float3 position = (ray.origin + tLeave * ray.direction - gParams.origin) / gParams.cellSize;
            int3 next = clamp(int3(floor(position)), nodeMin, nodeMin + (size - 1));
            next[leaveAxis] = ray.direction[leaveAxis] > 0.0 ? nodeMin[leaveAxis] + size : nodeMin[leaveAxis] - 1;
            if (next[leaveAxis] < 0 || next[leaveAxis] >= cells)
                break;

            // 4. Go on from the lowest node that holds both cells: the highest bit in which
            // the two cells differ names its level.
            const uint differ = uint((cell.x ^ next.x) | (cell.y ^ next.y) | (cell.z ^ next.z));
            level = uint(firstbithigh(differ));
            tile = tiles[level];
            cell = next;
        }
    }

    gHits[rayIndex] = closest.finish();
    closest.work.store(rayIndex);
}
