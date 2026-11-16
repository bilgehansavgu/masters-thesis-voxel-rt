// Primitive kind "box": every primitive is a solid axis-aligned box.
//
// THE PRIMITIVE CONTRACT. Every primitive kind (box, the voxel brick layouts) provides, in a
// file named primitive.slang in its own folder:
//
//   bool intersectPrimitive(Ray ray, uint primitiveId, out PrimitiveHit hit, inout WorkCounter work)
//       Returns true if `ray` hits primitive `primitiveId` with tMin <= t <= tMax, and the
//       closest such hit. A tracer calls it for every primitive whose box the ray touches.
//
//   uint primitiveAttributes(uint primitiveId, PrimitiveHit hit, inout WorkCounter work)
//       Returns what the kind stores for the hit point, as the bits of Hit.attributes
//       (thesis/shared/Hit.h). A tracer calls it once for each ray, for the closest hit only.
//
// PrimitiveHit is in shaders/common/types.slang. WorkCounter (shaders/common/work.slang) counts
// the work for diagnosis and costs nothing in the timed shaders. Binding 8 of set 0 holds the
// bounds (StructuredBuffer<Aabb>). Bindings 9..15 are the kind's own data, uploaded from
// core::Scene::primitiveData.
//
// Every tracer is compiled once per kind and only calls these two, through ClosestHit
// (shaders/common/closest.slang). A new kind changes what is inside a primitive without
// touching any tracer.

import types;
import work;
import aabb;
#include "thesis/shared/Bindings.h"

[[vk::binding(THESIS_BINDING_PRIMITIVE_BOUNDS, 0)]]
StructuredBuffer<Aabb> gPrimitiveBounds;

public bool intersectPrimitive(Ray ray, uint primitiveId, out PrimitiveHit hit, inout WorkCounter work)
{
    hit.detail = 0;
    return intersectAabb(ray, gPrimitiveBounds[primitiveId], hit.t, hit.face);
}

// A box stores nothing for a hit point. The shading uses the face of the box.
public uint primitiveAttributes(uint primitiveId, PrimitiveHit hit, inout WorkCounter work)
{
    return 0;
}
