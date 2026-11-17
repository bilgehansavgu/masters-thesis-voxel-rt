// The split bit layout of the 2012 brick format: the attributes are in their own buffer. Used
// by the primitive kinds brick2012_split and brick2012_split_mip.
// Bits: thesis/shared/Brick2012.h. Encoder: src/encodings/brick2012.

import brick2012;
#include "thesis/shared/Bindings.h"
#include "thesis/shared/Brick2012.h"

// 16-bit normals, two in each 32-bit word.
[[vk::binding(THESIS_BINDING_BRICK_ATTRIBUTES, 0)]]
StructuredBuffer<uint> gBrickAttributes;

public struct SplitLayout : IBrickLayout
{
    // A cell is one 16-bit word.
    public static uint cell(BrickRecord brick, uint index)
    {
        return word16(gBrickCells, brick.cellBase + index);
    }

    // A pointer cell holds the offset of the words of its column. The first of them is the
    // index of the column's first normal. The run words follow.
    public static uint firstRunWord(BrickRecord brick, uint cell)
    {
        return brick.indirectionBase + (cell >> THESIS_CELL_OFFSET_SHIFT) + 1;
    }

    // The normals of the height cells are in cell order. The indirection buffer starts with one
    // word for each row: the number of height cells in the rows before it. The code adds the
    // number of height cells that come before this cell in its own row.
    public static uint heightCellNormal(BrickRecord brick, uint brickSize, uint index, uint cell)
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
    public static uint pointerCellNormal(BrickRecord brick, uint cell, uint runCount, uint voxel)
    {
        const uint firstNormal = word16(gBrickIndirection, firstRunWord(brick, cell) - 1);
        return word16(gBrickAttributes, brick.attributeBase + firstNormal + voxel);
    }
};
