// ============================================================================
// READING ORDER: FILE 14B (after aabb.slang)
// ============================================================================
// Primitive kind "box": every primitive is a solid axis-aligned box.
//
// THE PRIMITIVE CONTRACT. Every primitive kind (box, the voxel brick layouts) provides, in a
// file named primitive.slang in its own folder:
//
//   struct PrimitiveHit { float t; uint face; }
//   bool intersectPrimitive(Ray ray, uint primitiveId, out PrimitiveHit hit)
//       Returns true if `ray` hits primitive `primitiveId` with tMin <= t <= tMax, and the
//       closest such hit. Binding 8 of set 0 holds the bounds (StructuredBuffer<Aabb>);
//       bindings 9..15 are the kind's own data, uploaded from core::Scene::primitiveData.
//
// Every tracer is compiled once per kind and only calls these, so a new kind changes what is
// inside a primitive without touching any tracer.
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
