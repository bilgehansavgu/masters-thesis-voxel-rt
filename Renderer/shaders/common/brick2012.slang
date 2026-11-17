// The brick test of the 2012 format (Reichl et al. 2012, section 4.1), shared by every bit layout
// of that format. Encoder: src/encodings/brick2012. Bits: thesis/shared/Brick2012.h.
//
// One primitive is one brick: N x N x N voxels, stored as a primary grid of N x N cells. A cell
// stands for one column of voxels along the primary axis of the brick. The test walks the ray
// across the primary grid, front to back (a DDA). In each cell it tests the ray against the
// filled voxels of that column. A filled voxel is a solid cube. A run (a row of filled voxels
// in one column) is one box, so it takes one test. With a min/max mipmap the walk skips whole
// squares of cells that the ray passes above or below.
//
// A bit layout only decides where the bits are. It provides the four functions of IBrickLayout
// (brick2012_inline.slang, brick2012_split.slang). A mipmap provides the two functions of
// IBrickMipmap (NoMipmap below, StoredMipmap in brick2012_mipmap.slang). The walk below is the
// same for all of them.

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
    public uint   mipmapLevels; // levels of the min/max mipmap of each brick; 0: no mipmap
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

// What a brick provides to skip regions of its primary grid: the min/max mipmap of Reichl et al.
// 2012. It is a stack of smaller grids over the primary grid. A cell of level L stands for a
// square of 2^L x 2^L cells of the primary grid, and holds the lowest and the highest height of
// a filled voxel in the columns of that square (bits: THESIS_MIP_* in Brick2012.h).
public interface IBrickMipmap
{
    // The highest level that is stored. 0: the encoding has no mipmap.
    static uint topLevel(BrickModelHeader header);

    // The cell at `position` of level `level` (1 .. topLevel) of brick `primitiveId`.
    static uint cell(uint primitiveId, uint brickSize, uint level, uint2 position);
};

// For an encoding without mipmap. The level of the walk below is then always 0, and the walk
// visits every cell on the way of the ray.
public struct NoMipmap : IBrickMipmap
{
    public static uint topLevel(BrickModelHeader header) { return 0; }
    public static uint cell(uint primitiveId, uint brickSize, uint level, uint2 position) { return 0; }
};

// PrimitiveHit.detail of a brick: the cell index in bits 0..9, the height of the voxel above.
static const uint kDetailCellMask = 0x3FF;
static const uint kDetailHeightShift = 10;

// One column of the brick, or one square of columns, while the ray is inside it.
struct ColumnPass
{
    float tIn;        // the ray is inside from tIn ...
    float tOut;       // ... to tOut
    float wIn;        // its height at tIn
    float wOut;       // its height at tOut
    float originW;    // height of the ray origin
    float directionW; // change of height for each unit of t

    // True if the ray is below `bottom` or above `top` all the way from tIn to tOut. The test
    // compares heights, not distances. The heights at tIn and tOut are the same numbers in two
    // neighbor cells, so no ray can pass between two cells unseen.
    bool passesBy(float bottom, float top)
    {
        return max(wIn, wOut) < bottom || min(wIn, wOut) > top;
    }

    // Tests the filled span from height `bottom` to height `top`, which is a box in the column.
    // Returns true if the ray meets it, with the distance of the first contact. `throughSide`
    // tells how the ray came in: through the side of the column (or it began inside the span),
    // or through the bottom or the top of the span.
    bool meets(float bottom, float top, out float t, out bool throughSide)
    {
        t = tIn;
        throughSide = true;
        if (passesBy(bottom, top))
            return false;
        if (wIn >= bottom && wIn <= top)
            return true;
        throughSide = false;
        t = clamp(((wIn < bottom ? bottom : top) - originW) / directionW, tIn, tOut);
        return true;
    }
};

// The ray in the two axes u and v of the primary grid. A border is a line between two rows or two
// columns of cells: border b along axis k is at position b.
struct GridRay
{
    float2 origin;
    float2 direction;
    float2 inverse; // 1 / direction; not used for a component of 0
    int2   step;    // +1, -1 or 0: the way the ray moves along u and along v

    // The distance at which the ray crosses border `border` along axis k. Every part of the walk
    // gets the distance of a border from this one place. So a border has one distance, whatever
    // the size of the squares that the walk looks at, and the distances along one axis are in order.
    float crossing(uint k, int border)
    {
        return (float(border) - origin[k]) * inverse[k];
    }

    // The distance at which the ray leaves, along axis k, the square of 2^level x 2^level cells
    // that holds `cell`. kNever if the ray does not move along k.
    float farBorder(uint k, int2 cell, uint level)
    {
        if (step[k] == 0)
            return kNever;
        const int squareLo = (cell[k] >> level) << level;
        return crossing(k, squareLo + (step[k] > 0 ? (1 << level) : 0));
    }

    // True if the ray crosses border `border` along axis j before the border along the other
    // axis that it crosses at distance `t`. When both distances are equal, the border along v
    // comes first. The walk uses this one order everywhere.
    bool crossesBefore(uint j, int border, float t)
    {
        const float tBorder = crossing(j, border);
        return j == 1 ? tBorder <= t : tBorder < t;
    }
};

// Returns true if `ray` hits a filled voxel of brick `primitiveId` with tMin <= t <= tMax, and
// the closest such hit.
//
// The walk goes across the primary grid front to back. It looks at one square of cells at a
// time. A square of level L has 2^L x 2^L cells, and level 0 is one cell.
//
//   - At a level above 0 it reads the mipmap cell of the square. If the ray passes above the
//     highest or below the lowest voxel of the square, no voxel in the square can be hit. The
//     walk then leaves the square in one step. Otherwise it goes one level down: it looks at
//     the smaller square that the ray is in.
//   - At level 0 it tests the ray against the filled voxels of the column.
//   - After it left a square, it goes on with the largest square that begins at the border that
//     the ray just crossed.
//
// Without mipmap the level is always 0. The walk then visits every cell on the way of the ray,
// which is the walk of Amanatides and Woo 1987.
//
// The mipmap changes the work and nothing else: the walk with mipmap visits the cells of the
// walk without it, in the same order and with the same distances, except those under a square
// that the ray passes by. So every encoding gives the same hit for the same ray. Each encoding is
// its own shader program, and the GPU compiler can round a distance differently in the last bit in
// two programs (tests/TestBrickTracing.cpp says when).
public bool intersectBrick<L : IBrickLayout, M : IBrickMipmap>(Ray ray, uint primitiveId, out PrimitiveHit hit,
                                                               inout WorkCounter work)
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

    // Set up the walk across the primary grid, in the two axes u and v.
    GridRay grid;
    grid.origin = float2(origin[axisU], origin[axisV]);
    grid.direction = float2(direction[axisU], direction[axisV]);
    grid.inverse = float2(0.0, 0.0);
    grid.step = int2(0, 0);
    for (uint k = 0; k < 2; ++k)
    {
        if (grid.direction[k] == 0.0)
            continue;
        grid.inverse[k] = 1.0 / grid.direction[k];
        grid.step[k] = grid.direction[k] > 0.0 ? 1 : -1;
    }
    // The cells inside the box: from loCell to hiCell - 1 along u and along v.
    const int2 loCell = int2(float2(lo[axisU], lo[axisV]));
    const int2 hiCell = int2(float2(hi[axisU], hi[axisV]));
    // The cell that the ray is in at the distance pass.tIn.
    int2 cell = clamp(int2(floor(grid.origin + tEnter * grid.direction)), loCell, hiCell - 1);

    ColumnPass pass;
    pass.originW = origin[axisW];
    pass.directionW = direction[axisW];
    pass.tIn = tEnter;
    uint sideAxis = enterAxis; // the axis of the cell border that the ray crossed last

    // Without mipmap topLevel is the constant 0. The compiler then removes every part of the
    // walk that is for a level above 0, and `level` is always 0.
    const uint topLevel = M.topLevel(header);
    const bool hasMipmap = topLevel > 0;
    uint level = topLevel;

    // The walk looks at the square of 2^level x 2^level cells that holds `cell`. tFar holds
    // where the ray leaves that square along u and along v. The walk keeps it up to date: it
    // computes a distance again only when the square changed along that axis.
    float2 tFar = float2(grid.farBorder(0, cell, level), grid.farBorder(1, cell, level));

    const uint maxSteps = 2 * n * (topLevel + 1); // more than any ray needs
    for (uint visited = 0; visited < maxSteps; ++visited)
    {
        // The ray leaves the square at the nearer of its two far borders, or at the end of the
        // ray inside the box. Rounding can put a border a little before tIn. The distances of
        // the walk never go back.
        pass.tOut = max(pass.tIn, min(min(tFar.x, tFar.y), tExit));
        pass.wIn = pass.originW + pass.tIn * pass.directionW;
        pass.wOut = pass.originW + pass.tOut * pass.directionW;

        if (hasMipmap && level > 0)
        {
            // The lowest and the highest filled voxel of the square.
            const uint range = M.cell(primitiveId, n, level, uint2(cell) >> level);
            const uint lowestVoxel = range & THESIS_MIP_LOW_MASK;
            const uint highestVoxel = (range >> THESIS_MIP_HIGH_SHIFT) & THESIS_MIP_HIGH_MASK;
            const float lowest = float(lowestVoxel);
            const float highest = float(highestVoxel);
            const bool mayHit = lowest <= highest && !pass.passesBy(lowest, highest + 1.0);
            work.mipmapCell(level, uint2((cell >> level) << level), lowestVoxel, highestVoxel, !mayHit, pass.tIn);
            if (mayHit)
            {
                // A voxel of the square can be on the way: look at the smaller square.
                level -= 1;
                tFar = float2(grid.farBorder(0, cell, level), grid.farBorder(1, cell, level));
                continue;
            }
        }
        else
        {
            const uint index = uint(cell.x) + n * uint(cell.y);
            const uint value = L.cell(brick, index);
            const uint kind = value & THESIS_CELL_KIND_MASK;
            const uint heightVoxel = (value >> THESIS_CELL_HEIGHT_SHIFT) & THESIS_CELL_HEIGHT_MASK;
            const bool isHeight = kind == THESIS_CELL_HEIGHT;
            work.cell(uint2(cell), kind, isHeight ? heightVoxel : 0, isHeight ? heightVoxel + 1 : 0, pass.tIn);

            bool found = false;
            float t = kNever;
            bool throughSide = false;
            float bottom = 0.0;
            float top = 0.0;
            if (kind == THESIS_CELL_HEIGHT)
            {
                bottom = float(heightVoxel);
                top = bottom + 1.0;
                found = pass.meets(bottom, top, t, throughSide);
            }
            else if (kind == THESIS_CELL_POINTER)
            {
                const uint firstRun = L.firstRunWord(brick, value);
                for (uint run = 0; run < n; ++run)
                {
                    const uint word = word16(gBrickIndirection, firstRun + run);
                    const uint runFirst = word & THESIS_RUN_START_MASK;
                    const uint runLength = ((word >> THESIS_RUN_LENGTH_SHIFT) & THESIS_RUN_LENGTH_MASK) + 1;
                    const float runBottom = float(runFirst);
                    const float runTop = runBottom + float(runLength);
                    float runT;
                    bool runThroughSide;
                    const bool met = pass.meets(runBottom, runTop, runT, runThroughSide);
                    work.run(runFirst, runLength, met, pass.tIn);
                    if (met && runT < t)
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
                work.voxelHit(uint2(cell), uint(height), hit.face, t);
                return true;
            }
        }

        // Leave the square. The squares come in order of distance, so the first hit is the closest.
        if (pass.tOut >= tExit)
            return false;
        // k: the axis of the far border that the ray crosses first. j: the other axis.
        const uint k = tFar.x < tFar.y ? 0 : 1;
        const uint j = 1 - k;
        const int size = 1 << level;
        const int2 squareLo = (cell >> level) << level;
        int2 next = cell;
        next[k] = grid.step[k] > 0 ? squareLo[k] + size : squareLo[k] - 1;
        if (hasMipmap && level > 0 && grid.step[j] != 0)
        {
            // The ray also moved along j while it crossed the square. Its cell along j follows
            // from the borders along j that it crossed before it left the square. The position
            // of the ray gives a first value. The two loops correct it by the distances of the
            // borders, so the result is the cell that a walk from cell to cell reaches.
            const int farSide = grid.step[j] > 0 ? 1 : 0;
            const int last = grid.step[j] > 0 ? squareLo[j] + size - 1 : squareLo[j];
            int c = clamp(int(floor(grid.origin[j] + tFar[k] * grid.direction[j])), squareLo[j], squareLo[j] + size - 1);
            c = grid.step[j] > 0 ? max(c, cell[j]) : min(c, cell[j]);
            for (int i = 0; i < size && c != last && grid.crossesBefore(j, c + farSide, tFar[k]); ++i)
                c += grid.step[j];
            for (int i = 0; i < size && c != cell[j] && !grid.crossesBefore(j, c - grid.step[j] + farSide, tFar[k]); ++i)
                c -= grid.step[j];
            next[j] = c;
        }
        if (any(next < loCell) || any(next >= hiCell))
            return false;

        if (hasMipmap)
        {
            // Go on with the largest square that begins at the border that the ray crossed: the
            // highest bit in which the old and the new cell differ along k gives its level.
            level = min(uint(firstbithigh(uint(cell[k] ^ next[k]))), topLevel);
            tFar[j] = grid.farBorder(j, next, level);
        }
        // Without mipmap the ray is in the same row or column of cells along j as before, so
        // its far border along j is the same.
        tFar[k] = grid.farBorder(k, next, level);
        cell = next;
        pass.tIn = pass.tOut;
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
