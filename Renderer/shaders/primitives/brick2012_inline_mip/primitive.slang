// Primitive kind "brick2012_inline_mip": the inline layout of the 2012 format, and a min/max
// mipmap for each brick that lets the brick test skip squares of cells.
// Encoder: src/encodings/brick2012. Bit layout: thesis/shared/Brick2012.h. The primitive
// contract is in ../box/primitive.slang.
//
// The test itself is in shaders/common/brick2012.slang. This file only names the layout and
// the mipmap that it uses.

import types;
import work;
import brick2012;
import brick2012_inline;
import brick2012_mipmap;

public bool intersectPrimitive(Ray ray, uint primitiveId, out PrimitiveHit hit, inout WorkCounter work)
{
    return intersectBrick<InlineLayout, StoredMipmap>(ray, primitiveId, hit, work);
}

public uint primitiveAttributes(uint primitiveId, PrimitiveHit hit, inout WorkCounter work)
{
    return brickAttributes<InlineLayout>(primitiveId, hit, work);
}
