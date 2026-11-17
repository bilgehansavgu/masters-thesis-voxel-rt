// The stored min/max mipmap of the 2012 brick format (Reichl et al. 2012, section 4.1). Used by
// the primitive kinds brick2012_inline_mip and brick2012_split_mip.
// Bits and the place of each cell: thesis/shared/Brick2012.h. Encoder: src/encodings/brick2012.

import brick2012;
#include "thesis/shared/Bindings.h"
#include "thesis/shared/Brick2012.h"

// 16-bit mipmap cells, two in each 32-bit word. Every brick has the same number of them.
[[vk::binding(THESIS_BINDING_BRICK_MIPMAP, 0)]]
StructuredBuffer<uint> gBrickMipmap;

public struct StoredMipmap : IBrickMipmap
{
    public static uint topLevel(BrickModelHeader header)
    {
        return header.mipmapLevels;
    }

    public static uint cell(uint primitiveId, uint brickSize, uint level, uint2 position)
    {
        const uint cellsOfBrick = THESIS_MIP_CELLS_OF_BRICK(brickSize);
        const uint levelStart = THESIS_MIP_LEVEL_START(brickSize, level);
        const uint index = levelStart + position.x + (brickSize >> level) * position.y;
        return word16(gBrickMipmap, primitiveId * cellsOfBrick + index);
    }
};
