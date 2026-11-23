// Primitive kind "bitmask": one primitive is one voxel brick of the baseline format, one occupancy
// bit for every voxel. Encoder: src/encodings/bitmask. Bit layout: thesis/bitmask/BitmaskShared.h. The
// primitive contract is in ../box/primitive.slang.
//
// The brick test walks the ray across the columns of the brick, front to back, like the walk of
// the 2012 format (shaders/common/brick2012.slang) without mipmap. It computes every distance with
// the same formulas, so both formats give the same hit for the same ray. Inside a column it does
// not test runs: it masks the bits of the voxels between the heights at which the ray enters and
// leaves the column, and takes the first set bit in the direction of the ray.
// WHY a walk of its own and not the 2012 one: that walk reads cells, runs and their bindings,
// which this format does not have.

import types;
import work;
import bounds;
#include "thesis/bitmask/BitmaskShared.h"
#include "thesis/shared/Bindings.h"
#include "thesis/shared/Brick2012.h" // THESIS_CELL_*: the column kinds that the ray log names
#include "thesis/shared/Hit.h"

// Buffer "header". CPU mirror: thesis/bitmask/BitmaskBuffers.h.
public struct BitmaskModelHeader
{
    public float3 origin; // corner of voxel (0, 0, 0) of the whole grid
    public float  voxelSize;
    public uint3  gridSize; // voxels along each axis
    public uint   brickSize; // N
    public uint   brickCount;
    public uint   voxelCount;
    public uint   reserved0;
    public uint   reserved1;
};

// Buffer "brick table": one record for each brick, at the index of its primitive.
public struct BitmaskBrickRecord
{
    public uint position;   // brick coordinate, see THESIS_BITMASK_COORDINATE_*
    public uint normalBase; // index of the brick's first normal
};

[[vk::binding(THESIS_BINDING_BITMASK_HEADER, 0)]]
StructuredBuffer<BitmaskModelHeader> gBitmaskHeader;
[[vk::binding(THESIS_BINDING_BITMASK_TABLE, 0)]]
StructuredBuffer<BitmaskBrickRecord> gBitmaskTable;
[[vk::binding(THESIS_BINDING_BITMASK_MASKS, 0)]]
StructuredBuffer<uint> gBitmaskMasks;
// 16-bit words, two in each 32-bit word.
[[vk::binding(THESIS_BINDING_BITMASK_ROW_COUNTS, 0)]]
StructuredBuffer<uint> gBitmaskRowCounts;
// 16-bit words, two in each 32-bit word.
[[vk::binding(THESIS_BINDING_BITMASK_NORMALS, 0)]]
StructuredBuffer<uint> gBitmaskNormals;

// Columns run along x: the height w is x, the cell (u, v) is (y, z).
static const uint kAxisW = 0;
static const uint kAxisU = 1;
static const uint kAxisV = 2;

// PrimitiveHit.detail of a brick: the cell index in bits 0..9, the height of the voxel above.
static const uint kDetailCellMask = 0x3FF;
static const uint kDetailHeightShift = 10;

uint word16(StructuredBuffer<uint> buffer, uint index)
{
    return (buffer[index >> 1] >> ((index & 1) << 4)) & 0xFFFF;
}

// The N occupancy bits of column `cell` of brick `primitiveId`: bit h is the voxel at height h.
uint columnBits(uint primitiveId, uint n, uint cell)
{
    const uint bit = n * cell;
    const uint word = gBitmaskMasks[primitiveId * (n * n * n / 32) + (bit >> 5)];
    return n == 32 ? word : (word >> (bit & 31)) & ((1u << n) - 1u);
}

// Bits first .. last, with 0 <= first <= last <= 31.
uint bitsFromTo(uint first, uint last)
{
    const uint upTo = last >= 31 ? 0xFFFFFFFFu : (1u << (last + 1)) - 1u;
    return upTo & ~((1u << first) - 1u);
}

bool isSet(uint bits, int height)
{
    return height >= 0 && height < 32 && ((bits >> uint(height)) & 1u) != 0;
}

// The distance at which the ray crosses, along axis k of the grid of columns, the far border of
// `cell`. kNever if the ray does not move along k. The same formula as GridRay.crossing in the
// 2012 walk.
float farBorder(float2 origin, float2 inverse, int2 step, uint k, int2 cell)
{
    if (step[k] == 0)
        return kNever;
    return (float(cell[k] + (step[k] > 0 ? 1 : 0)) - origin[k]) * inverse[k];
}

public bool intersectPrimitive(Ray ray, uint primitiveId, out PrimitiveHit hit, inout WorkCounter work)
{
    hit.t = 0.0;
    hit.face = 0;
    hit.detail = 0;

    const BitmaskModelHeader header = gBitmaskHeader[0];
    const BitmaskBrickRecord brick = gBitmaskTable[primitiveId];
    const uint n = header.brickSize;

    // The ray in the voxel coordinates of the brick: the brick is the cube from 0 to N and one
    // voxel has size 1. The distance t keeps its meaning.
    const uint3 coordinate =
        uint3(brick.position & THESIS_BITMASK_COORDINATE_MASK,
              (brick.position >> THESIS_BITMASK_COORDINATE_BITS) & THESIS_BITMASK_COORDINATE_MASK,
              (brick.position >> (2 * THESIS_BITMASK_COORDINATE_BITS)) & THESIS_BITMASK_COORDINATE_MASK);
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
    uint enterAxis = kAxisW;
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

    // The walk across the grid of columns, in the two axes u and v.
    const float2 gridOrigin = float2(origin[kAxisU], origin[kAxisV]);
    const float2 gridDirection = float2(direction[kAxisU], direction[kAxisV]);
    float2 inverse = float2(0.0, 0.0);
    int2 step = int2(0, 0);
    for (uint k = 0; k < 2; ++k)
    {
        if (gridDirection[k] == 0.0)
            continue;
        inverse[k] = 1.0 / gridDirection[k];
        step[k] = gridDirection[k] > 0.0 ? 1 : -1;
    }
    // The columns inside the box: from loCell to hiCell - 1 along u and along v.
    const int2 loCell = int2(float2(lo[kAxisU], lo[kAxisV]));
    const int2 hiCell = int2(float2(hi[kAxisU], hi[kAxisV]));
    int2 cell = clamp(int2(floor(gridOrigin + tEnter * gridDirection)), loCell, hiCell - 1);

    const float originW = origin[kAxisW];
    const float directionW = direction[kAxisW];
    float tIn = tEnter;
    uint sideAxis = enterAxis; // the axis of the column border that the ray crossed last
    float2 tFar = float2(farBorder(gridOrigin, inverse, step, 0, cell), farBorder(gridOrigin, inverse, step, 1, cell));

    for (uint visited = 0; visited < 2 * n; ++visited) // more than any ray needs
    {
        // The ray leaves the column at the nearer of its two far borders, or at the end of the
        // ray inside the box. The distances of the walk never go back.
        const float tOut = max(tIn, min(min(tFar.x, tFar.y), tExit));
        const float wIn = originW + tIn * directionW;
        const float wOut = originW + tOut * directionW;

        const uint index = uint(cell.x) + n * uint(cell.y);
        const uint bits = columnBits(primitiveId, n, index);
        const uint filled = countbits(bits);
        const uint kind = filled == 0 ? THESIS_CELL_EMPTY : (filled == 1 ? THESIS_CELL_HEIGHT : THESIS_CELL_POINTER);
        const uint lowest = firstbitlow(bits);
        work.cell(uint2(cell), kind, filled == 1 ? lowest : 0, filled == 1 ? lowest + 1 : 0, tIn);

        // The voxels the ray touches in this column: those from height hFirst to hLast. A voxel
        // that the ray only touches at its top or bottom side counts, as in the 2012 walk.
        const float wLow = min(wIn, wOut);
        const float wHigh = max(wIn, wOut);
        const int hFirst = max(int(ceil(wLow)) - 1, 0);
        const int hLast = min(int(floor(wHigh)), int(n) - 1);
        const uint touched = hFirst <= hLast ? bits & bitsFromTo(uint(hFirst), uint(hLast)) : 0u;
        if (touched != 0)
        {
            // Inside a filled voxel where the ray enters the column: it hits that voxel through
            // the side of the column (or it began inside it). Otherwise it meets the first filled
            // voxel above (moving up) or below (moving down) through its bottom or top side.
            const int hEnter = int(floor(wIn));
            int height = -1;
            if (isSet(bits, hEnter))
                height = hEnter;
            else if (float(hEnter) == wIn && isSet(bits, hEnter - 1))
                height = hEnter - 1;
            const bool throughSide = height >= 0;
            float t = tIn;
            if (!throughSide)
            {
                height = directionW > 0.0 ? int(firstbitlow(touched)) : int(firstbithigh(touched));
                const float side = directionW > 0.0 ? float(height) : float(height + 1);
                t = clamp((side - originW) / directionW, tIn, tOut);
            }
            const uint faceAxis = throughSide ? sideAxis : kAxisW;
            hit.t = t;
            hit.face = faceAxis * 2 + (direction[faceAxis] > 0.0 ? 0 : 1);
            hit.detail = index | (uint(height) << kDetailHeightShift);
            work.voxelHit(uint2(cell), uint(height), hit.face, t);
            return true;
        }

        // Leave the column. The columns come in order of distance, so the first hit is the
        // closest. When both far borders are at the same distance, the border along v comes first.
        if (tOut >= tExit)
            return false;
        const uint k = tFar.x < tFar.y ? 0 : 1;
        int2 next = cell;
        next[k] += step[k];
        if (any(next < loCell) || any(next >= hiCell))
            return false;
        tFar[k] = farBorder(gridOrigin, inverse, step, k, next);
        cell = next;
        tIn = tOut;
        sideAxis = k == 0 ? kAxisU : kAxisV;
    }
    return false;
}

// The stored normal of the voxel that `hit` names. Its place among the normals of the brick is
// the number of filled voxels before it: the row count of its row, the voxels of the columns
// before it in that row, and the voxels below it in its own column.
public uint primitiveAttributes(uint primitiveId, PrimitiveHit hit, inout WorkCounter work)
{
    const BitmaskBrickRecord brick = gBitmaskTable[primitiveId];
    const uint n = gBitmaskHeader[0].brickSize;
    const uint index = hit.detail & kDetailCellMask;
    const uint height = hit.detail >> kDetailHeightShift;
    const uint row = index / n;

    uint normal = brick.normalBase + word16(gBitmaskRowCounts, primitiveId * n + row);
    for (uint earlier = row * n; earlier < index; ++earlier)
        normal += countbits(columnBits(primitiveId, n, earlier));
    const uint bits = columnBits(primitiveId, n, index);
    normal += countbits(bits & ((1u << height) - 1u));

    // The kind of the column, as in figure 7 of Reichl et al. 2012: one voxel, one run, or more.
    const uint runs = countbits(bits & ~(bits << 1));
    work.hitCell(countbits(bits) == 1 ? THESIS_HIT_CELL_HEIGHT
                                      : (runs == 1 ? THESIS_HIT_CELL_ONE_RUN : THESIS_HIT_CELL_SEVERAL_RUNS));
    return THESIS_HIT_HAS_NORMAL | word16(gBitmaskNormals, normal);
}
