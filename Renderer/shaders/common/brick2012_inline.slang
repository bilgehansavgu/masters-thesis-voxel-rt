// The inline bit layout of the 2012 brick format: the attributes are next to the heights (the
// order of the paper). Used by the primitive kinds brick2012_inline and brick2012_inline_mip.
// Bits: thesis/shared/Brick2012.h. Encoder: src/encodings/brick2012.

import brick2012;
#include "thesis/shared/Brick2012.h"

public struct InlineLayout : IBrickLayout
{
    // A cell is one 32-bit word.
    public static uint cell(BrickRecord brick, uint index)
    {
        return gBrickCells[brick.cellBase + index];
    }

    // A pointer cell holds the offset of the run words of its column.
    public static uint firstRunWord(BrickRecord brick, uint cell)
    {
        return brick.indirectionBase + (cell >> THESIS_CELL_OFFSET_SHIFT);
    }

    // The normal is in the cell, next to the height.
    public static uint heightCellNormal(BrickRecord brick, uint brickSize, uint index, uint cell)
    {
        return (cell >> THESIS_CELL_NORMAL_SHIFT) & THESIS_CELL_NORMAL_MASK;
    }

    // The normals of a column follow its run words, one for each voxel.
    public static uint pointerCellNormal(BrickRecord brick, uint cell, uint runCount, uint voxel)
    {
        return word16(gBrickIndirection, firstRunWord(brick, cell) + runCount + voxel);
    }
};
