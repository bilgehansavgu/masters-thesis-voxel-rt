// Primitive kind "brick2012_inline": one primitive is one voxel brick in the 2012 format, with
// the attributes next to the heights (the order of the paper). Encoder: src/encodings/brick2012.
// Bit layout: thesis/shared/Brick2012.h. The primitive contract is in ../box/primitive.slang.
//
// The test itself is in shaders/common/brick2012.slang and is the same for every layout of the
// 2012 format. This file only says where the inline layout keeps its bits.

import types;
import work;
import brick2012;
#include "thesis/shared/Brick2012.h"

struct InlineLayout : IBrickLayout
{
    // A cell is one 32-bit word.
    static uint cell(BrickRecord brick, uint index)
    {
        return gBrickCells[brick.cellBase + index];
    }

    // A pointer cell holds the offset of the run words of its column.
    static uint firstRunWord(BrickRecord brick, uint cell)
    {
        return brick.indirectionBase + (cell >> THESIS_CELL_OFFSET_SHIFT);
    }

    // The normal is in the cell, next to the height.
    static uint heightCellNormal(BrickRecord brick, uint brickSize, uint index, uint cell)
    {
        return (cell >> THESIS_CELL_NORMAL_SHIFT) & THESIS_CELL_NORMAL_MASK;
    }

    // The normals of a column follow its run words, one for each voxel.
    static uint pointerCellNormal(BrickRecord brick, uint cell, uint runCount, uint voxel)
    {
        return word16(gBrickIndirection, firstRunWord(brick, cell) + runCount + voxel);
    }
};

public bool intersectPrimitive(Ray ray, uint primitiveId, out PrimitiveHit hit, inout WorkCounter work)
{
    return intersectBrick<InlineLayout>(ray, primitiveId, hit, work);
}

public uint primitiveAttributes(uint primitiveId, PrimitiveHit hit, inout WorkCounter work)
{
    return brickAttributes<InlineLayout>(primitiveId, hit, work);
}
