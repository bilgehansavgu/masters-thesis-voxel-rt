// Primitive kind "brick2012_inline": one primitive is one voxel brick in the 2012 format, with
// the attributes next to the heights (the order of the paper). Encoder: src/encodings/brick2012.
// Bit layout: thesis/shared/Brick2012.h. The primitive contract is in ../box/primitive.slang.
//
// The test itself is in shaders/common/brick2012.slang and is the same for every layout of the
// 2012 format. This file only names the layout, and that the bricks have no mipmap.

import types;
import work;
import brick2012;
import brick2012_inline;

public bool intersectPrimitive(Ray ray, uint primitiveId, out PrimitiveHit hit, inout WorkCounter work)
{
    return intersectBrick<InlineLayout, NoMipmap>(ray, primitiveId, hit, work);
}

public uint primitiveAttributes(uint primitiveId, PrimitiveHit hit, inout WorkCounter work)
{
    return brickAttributes<InlineLayout>(primitiveId, hit, work);
}
