// The brick test of the 2012 format (Reichl et al. 2012, section 4.1), shared by every bit layout
// of that format. Encoder: src/encodings/brick2012. Bits: thesis/shared/Brick2012.h.
//
// One primitive is one brick: N x N x N voxels, stored as a primary grid of N x N cells. A cell
// stands for one column of voxels along the primary axis of the brick. The test walks the ray
// across the primary grid, one cell at a time and front to back (a DDA). In each cell it tests
// the ray against the filled voxels of that column. A filled voxel is a solid cube. A run (a
// row of filled voxels in one column) is one box, so it takes one test.
//
// A bit layout only decides where the bits are. It provides the four functions of IBrickLayout,
// and the walk below is the same for all of them.

import types;
import work;
#include "thesis/shared/Bindings.h"
#include "thesis/shared/Brick2012.h"
#include "thesis/shared/Hit.h"

// Buffer "header". CPU mirror: thesis/brick2012/BrickBuffers.h.
public struct BrickModelHeader
{
    public float3 origin; // corner of voxel (0, 0, 0) of the whole grid
    public float  voxelSize;
    public uint3  gridSize; // voxels along each axis
    public uint   brickSize; // N
    public uint   brickCount;
    public uint   layout;
    public uint   reserved0;
    public uint   reserved1;
};

// Buffer "brick table": one record for each brick, at the index of its primitive.
public struct BrickRecord
{
    public uint position;        // brick coordinate and primary axis, see THESIS_BRICK_*
    public uint cellBase;        // index of the brick's first cell in the cell buffer
    public uint indirectionBase; // index of the brick's first word in the indirection buffer
    public uint attributeBase;   // split layout: index of the brick's first normal
};

[[vk::binding(THESIS_BINDING_PRIMITIVE_BOUNDS, 0)]]
public StructuredBuffer<Aabb> gPrimitiveBounds;
[[vk::binding(THESIS_BINDING_BRICK_HEADER, 0)]]
public StructuredBuffer<BrickModelHeader> gBrickHeader;
[[vk::binding(THESIS_BINDING_BRICK_TABLE, 0)]]
public StructuredBuffer<BrickRecord> gBrickTable;
// 32-bit words. The inline layout has one cell in a word, the split layout has two.
[[vk::binding(THESIS_BINDING_BRICK_CELLS, 0)]]
public StructuredBuffer<uint> gBrickCells;
// 16-bit words, two in each 32-bit word.
[[vk::binding(THESIS_BINDING_BRICK_INDIRECTION, 0)]]
public StructuredBuffer<uint> gBrickIndirection;

// The 16-bit word at `index` of a buffer that holds two of them in each 32-bit word.
public uint word16(StructuredBuffer<uint> buffer, uint index)
{
    return (buffer[index >> 1] >> ((index & 1) << 4)) & 0xFFFF;
}

// What a bit layout provides.
public interface IBrickLayout
{
    // The cell at `index` (u + N * v) of the brick's primary grid. In every layout the kind is
    // in bits 0..1 and the height (height cell) or the offset (pointer cell) starts at bit 2.
    static uint cell(BrickRecord brick, uint index);

    // Index in the indirection buffer of the first run word of the column of pointer cell `cell`.
    static uint firstRunWord(BrickRecord brick, uint cell);

    // The packed normal of the one voxel of the height cell `cell` at `index`.
    static uint heightCellNormal(BrickRecord brick, uint brickSize, uint index, uint cell);

    // The packed normal of a voxel in the column of pointer cell `cell`. The column has
    // `runCount` runs, and `voxel` counts the filled voxels of the column from the lowest (0).
    static uint pointerCellNormal(BrickRecord brick, uint cell, uint runCount, uint voxel);
};

// PrimitiveHit.detail of a brick: the cell index in bits 0..9, the height of the voxel above.
static const uint kDetailCellMask = 0x3FF;
static const uint kDetailHeightShift = 10;

// One column of the brick while the ray is inside it.
struct ColumnPass
{
    float tIn;        // the ray is inside the column from tIn ...
    float tOut;       // ... to tOut
    float wIn;        // its height at tIn
    float wOut;       // its height at tOut
    float originW;    // height of the ray origin
    float directionW; // change of height for each unit of t

    // Tests the filled span from height `bottom` to height `top`, which is a box in the column.
    // Returns true if the ray meets it, with the distance of the first contact. `throughSide`
    // tells how the ray came in: through the side of the column (or it began inside the span),
    // or through the bottom or the top of the span.
    //
    // The test compares heights, not distances. The heights at tIn and tOut are the same
    // numbers in two neighbor cells, so no ray can pass between two cells unseen.
    bool meets(float bottom, float top, out float t, out bool throughSide)
    {
        t = tIn;
        throughSide = true;
        if (max(wIn, wOut) < bottom || min(wIn, wOut) > top)
            return false;
        if (wIn >= bottom && wIn <= top)
            return true;
        throughSide = false;
        t = clamp(((wIn < bottom ? bottom : top) - originW) / directionW, tIn, tOut);
        return true;
    }
};

// Returns true if `ray` hits a filled voxel of brick `primitiveId` with tMin <= t <= tMax, and
// the closest such hit.
public bool intersectBrick<L : IBrickLayout>(Ray ray, uint primitiveId, out PrimitiveHit hit, inout WorkCounter work)
{
    hit.t = 0.0;
    hit.face = 0;
    hit.detail = 0;

    const BrickModelHeader header = gBrickHeader[0];
    const BrickRecord brick = gBrickTable[primitiveId];
    const uint n = header.brickSize;

    // w is the primary axis (the height). u and v span the primary grid.
    const uint axisW = brick.position >> THESIS_BRICK_AXIS_SHIFT;
    const uint axisU = (axisW + 1) % 3;
    const uint axisV = (axisW + 2) % 3;

    // The ray in the voxel coordinates of the brick: the brick is the cube from 0 to N and one
    // voxel has size 1. The distance t keeps its meaning.
    const uint3 coordinate = uint3(brick.position & THESIS_BRICK_COORDINATE_MASK,
                                   (brick.position >> THESIS_BRICK_COORDINATE_BITS) & THESIS_BRICK_COORDINATE_MASK,
                                   (brick.position >> (2 * THESIS_BRICK_COORDINATE_BITS)) & THESIS_BRICK_COORDINATE_MASK);
    const float3 brickOrigin = header.origin + header.voxelSize * float3(coordinate * n);
    const float inverseVoxelSize = 1.0 / header.voxelSize;
    const float3 origin = (ray.origin - brickOrigin) * inverseVoxelSize;
    const float3 direction = ray.direction * inverseVoxelSize;

    // The box around the filled voxels of the brick. Its sides are whole voxel numbers.
    const Aabb bounds = gPrimitiveBounds[primitiveId];
    const float3 lo = round((bounds.lo() - brickOrigin) * inverseVoxelSize);
    const float3 hi = round((bounds.hi() - brickOrigin) * inverseVoxelSize);

    // The part of the ray inside that box: from tEnter to tExit (slab test).
    float tEnter = ray.tMin;
    float tExit = ray.tMax;
    uint enterAxis = axisW;
    for (uint axis = 0; axis < 3; ++axis)
    {
        if (direction[axis] == 0.0)
        {
            if (origin[axis] < lo[axis] || origin[axis] > hi[axis])
                return false;
            continue;
        }
        const float tA = (lo[axis] - origin[axis]) / direction[axis];
        const float tB = (hi[axis] - origin[axis]) / direction[axis];
        if (min(tA, tB) > tEnter)
        {
            tEnter = min(tA, tB);
            enterAxis = axis;
        }
        tExit = min(tExit, max(tA, tB));
    }
    if (tEnter > tExit)
        return false;

    // Set up the walk across the primary grid (Amanatides and Woo 1987), in the two axes u and v.
    const float2 origin2 = float2(origin[axisU], origin[axisV]);
    const float2 direction2 = float2(direction[axisU], direction[axisV]);
    const float2 lo2 = float2(lo[axisU], lo[axisV]);
    const float2 hi2 = float2(hi[axisU], hi[axisV]);
    const float2 start = origin2 + tEnter * direction2;
    int2 cell = int2(clamp(floor(start), lo2, hi2 - 1.0));
    int2 step = int2(0, 0);
    float2 tNext = float2(kNever, kNever);  // where the ray leaves the current cell along u and along v
    float2 tDelta = float2(kNever, kNever); // distance between two cell borders along u and along v
    for (uint k = 0; k < 2; ++k)
    {
        if (direction2[k] == 0.0)
            continue;
        step[k] = direction2[k] > 0.0 ? 1 : -1;
        tDelta[k] = abs(1.0 / direction2[k]);
        tNext[k] = (float(cell[k] + (direction2[k] > 0.0 ? 1 : 0)) - origin2[k]) / direction2[k];
    }

    ColumnPass pass;
    pass.originW = origin[axisW];
    pass.directionW = direction[axisW];
    pass.tIn = tEnter;
    uint sideAxis = enterAxis; // the axis of the cell border that the ray crossed last

    for (uint visited = 0; visited < 2 * n; ++visited)
    {
        work.cell();
        pass.tOut = min(min(tNext.x, tNext.y), tExit);
        pass.wIn = pass.originW + pass.tIn * pass.directionW;
        pass.wOut = pass.originW + pass.tOut * pass.directionW;

        const uint index = uint(cell.x) + n * uint(cell.y);
        const uint value = L.cell(brick, index);
        const uint kind = value & THESIS_CELL_KIND_MASK;

        bool found = false;
        float t = kNever;
        bool throughSide = false;
        float bottom = 0.0;
        float top = 0.0;
        if (kind == THESIS_CELL_HEIGHT)
        {
            bottom = float((value >> THESIS_CELL_HEIGHT_SHIFT) & THESIS_CELL_HEIGHT_MASK);
            top = bottom + 1.0;
            found = pass.meets(bottom, top, t, throughSide);
        }
        else if (kind == THESIS_CELL_POINTER)
        {
            const uint firstRun = L.firstRunWord(brick, value);
            for (uint run = 0; run < n; ++run)
            {
                work.run();
                const uint word = word16(gBrickIndirection, firstRun + run);
                const float runBottom = float(word & THESIS_RUN_START_MASK);
                const float runTop = runBottom + float(((word >> THESIS_RUN_LENGTH_SHIFT) & THESIS_RUN_LENGTH_MASK) + 1);
                float runT;
                bool runThroughSide;
                if (pass.meets(runBottom, runTop, runT, runThroughSide) && runT < t)
                {
                    found = true;
                    t = runT;
                    throughSide = runThroughSide;
                    bottom = runBottom;
                    top = runTop;
                }
                if ((word & THESIS_RUN_LAST_BIT) != 0)
                    break;
            }
        }

        if (found)
        {
            // The voxel of the span that the ray touched first.
            float height = pass.wIn < bottom ? bottom : top - 1.0;
            if (throughSide)
                height = clamp(floor(pass.wIn), bottom, top - 1.0);
            const uint faceAxis = throughSide ? sideAxis : axisW;
            hit.t = t;
            hit.face = faceAxis * 2 + (direction[faceAxis] > 0.0 ? 0 : 1);
            hit.detail = index | (uint(height) << kDetailHeightShift);
            return true;
        }

        // Go to the next cell. The cells come in order of distance, so the first hit is the closest.
        if (pass.tOut >= tExit)
            return false;
        const uint k = tNext.x < tNext.y ? 0 : 1;
        cell[k] += step[k];
        if (float(cell[k]) < lo2[k] || float(cell[k]) >= hi2[k])
            return false;
        pass.tIn = tNext[k];
        tNext[k] += tDelta[k];
        sideAxis = k == 0 ? axisU : axisV;
    }
    return false;
}

// The attributes of the voxel that `hit` names: the stored normal. Reads the cell again and,
// for a pointer cell, the runs of the column, to find the place of the normal.
public uint brickAttributes<L : IBrickLayout>(uint primitiveId, PrimitiveHit hit, inout WorkCounter work)
{
    const BrickRecord brick = gBrickTable[primitiveId];
    const uint n = gBrickHeader[0].brickSize;
    const uint index = hit.detail & kDetailCellMask;
    const uint height = hit.detail >> kDetailHeightShift;
    const uint value = L.cell(brick, index);

    if ((value & THESIS_CELL_KIND_MASK) == THESIS_CELL_HEIGHT)
    {
        work.hitCell(THESIS_HIT_CELL_HEIGHT);
        return THESIS_HIT_HAS_NORMAL | L.heightCellNormal(brick, n, index, value);
    }

    // Pointer cell: count the filled voxels of the column below the hit voxel, and the runs.
    const uint firstRun = L.firstRunWord(brick, value);
    uint voxel = 0;
    uint voxelsBefore = 0;
    uint runCount = 0;
    for (uint run = 0; run < n; ++run)
    {
        const uint word = word16(gBrickIndirection, firstRun + run);
        const uint first = word & THESIS_RUN_START_MASK;
        const uint length = ((word >> THESIS_RUN_LENGTH_SHIFT) & THESIS_RUN_LENGTH_MASK) + 1;
        if (height >= first && height < first + length)
            voxel = voxelsBefore + (height - first);
        voxelsBefore += length;
        runCount = run + 1;
        if ((word & THESIS_RUN_LAST_BIT) != 0)
            break;
    }
    work.hitCell(runCount == 1 ? THESIS_HIT_CELL_ONE_RUN : THESIS_HIT_CELL_SEVERAL_RUNS);
    return THESIS_HIT_HAS_NORMAL | L.pointerCellNormal(brick, value, runCount, voxel);
}
