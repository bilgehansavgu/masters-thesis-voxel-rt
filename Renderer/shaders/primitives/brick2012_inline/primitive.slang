// Primitive kind "brick2012_inline": one primitive is one voxel brick in the 2012 format, with
// the attributes next to the heights. Encoder: src/encodings/brick2012. Bit layout:
// shared/include/thesis/shared/Brick2012.h. The primitive contract is in ../box/primitive.slang.
//
// STEP 1 STATE: the test stops at the box around the filled voxels of the brick, so the image
// shows the bricks, not the voxels. The buffers of the encoding (bindings 9..12) are uploaded
// and bound, but nothing reads them yet. Step 2 adds the walk through the primary grid.

import types;
import aabb;
#include "thesis/shared/Bindings.h"

[[vk::binding(THESIS_BINDING_PRIMITIVE_BOUNDS, 0)]]
StructuredBuffer<Aabb> gPrimitiveBounds;

public struct PrimitiveHit
{
    public float t;
    public uint  face;
};

public bool intersectPrimitive(Ray ray, uint primitiveId, out PrimitiveHit hit)
{
    return intersectAabb(ray, gPrimitiveBounds[primitiveId], hit.t, hit.face);
}
