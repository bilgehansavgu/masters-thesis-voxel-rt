// ============================================================================
// READING ORDER: FILE 14D (alongside brick2012_inline/primitive.slang)
// ============================================================================
// Primitive kind "brick2012_split": one primitive is one voxel brick in the 2012 format, with
// the attributes in their own buffer. Encoder: src/encodings/brick2012. Bit layout:
// shared/include/thesis/shared/Brick2012.h. The primitive contract is in ../box/primitive.slang.
//
// STEP 1 STATE: the test stops at the tight AABB around the filled voxels of the brick, so the
// rendered image shows the outer brick boxes rather than individual voxels inside the brick.
// The buffers of the encoding (bindings 9..13) are already uploaded to the GPU and bound to
// descriptor set 0, ready for Step 2 (which adds the 2D DDA walk through the primary grid).
// ============================================================================

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
