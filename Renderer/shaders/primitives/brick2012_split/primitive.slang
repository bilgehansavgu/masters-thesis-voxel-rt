// Primitive kind "brick2012_split": one primitive is one voxel brick in the 2012 format, with
// the attributes in their own buffer. Encoder: src/encodings/brick2012. Bit layout:
// thesis/shared/Brick2012.h. The primitive contract is in ../box/primitive.slang.
//
// The test itself is in shaders/common/brick2012.slang and is the same for every layout of the
// 2012 format. This file only says where the split layout keeps its bits.

import types;
import work;
import brick2012;
#include "thesis/shared/Bindings.h"
#include "thesis/shared/Brick2012.h"

// 16-bit normals, two in each 32-bit word.
[[vk::binding(THESIS_BINDING_BRICK_ATTRIBUTES, 0)]]
StructuredBuffer<uint> gBrickAttributes;

struct SplitLayout : IBrickLayout
{
    // A cell is one 16-bit word.
    static uint cell(BrickRecord brick, uint index)
    {
        return word16(gBrickCells, brick.cellBase + index);
    }

    // A pointer cell holds the offset of the words of its column. The first of them is the
    // index of the column's first normal. The run words follow.
    static uint firstRunWord(BrickRecord brick, uint cell)
    {
        return brick.indirectionBase + (cell >> THESIS_CELL_OFFSET_SHIFT) + 1;
    }

    // The normals of the height cells are in cell order. The indirection buffer starts with one
    // word for each row: the number of height cells in the rows before it. The code adds the
    // number of height cells that come before this cell in its own row.
    static uint heightCellNormal(BrickRecord brick, uint brickSize, uint index, uint cell)
    {
        const uint row = index / brickSize;
        uint normal = word16(gBrickIndirection, brick.indirectionBase + row);
        for (uint earlier = row * brickSize; earlier < index; ++earlier)
        {
            if ((SplitLayout.cell(brick, earlier) & THESIS_CELL_KIND_MASK) == THESIS_CELL_HEIGHT)
                normal += 1;
        }
        return word16(gBrickAttributes, brick.attributeBase + normal);
    }

    // The word before the run words is the index of the column's first normal.
    static uint pointerCellNormal(BrickRecord brick, uint cell, uint runCount, uint voxel)
    {
        const uint firstNormal = word16(gBrickIndirection, firstRunWord(brick, cell) - 1);
        return word16(gBrickAttributes, brick.attributeBase + firstNormal + voxel);
    }
};

public bool intersectPrimitive(Ray ray, uint primitiveId, out PrimitiveHit hit, inout WorkCounter work)
{
    return intersectBrick<SplitLayout>(ray, primitiveId, hit, work);
}

public uint primitiveAttributes(uint primitiveId, PrimitiveHit hit, inout WorkCounter work)
{
    return brickAttributes<SplitLayout>(primitiveId, hit, work);
}
