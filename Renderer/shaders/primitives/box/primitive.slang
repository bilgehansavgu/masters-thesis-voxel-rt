// Primitive kind "box": every primitive is a solid axis-aligned box.
//
// THE PRIMITIVE CONTRACT. Every primitive kind (later: the encoded voxel brick) provides, in a
// file named primitive.slang in its own folder:
//
//   struct PrimitiveHit { float t; uint face; }
//   bool intersectPrimitive(Ray ray, uint primitiveId, out PrimitiveHit hit)
//       Returns true if `ray` hits primitive `primitiveId` with tMin <= t <= tMax, and the
//       closest such hit. Binding 8 of set 0 holds the bounds (StructuredBuffer<Aabb>);
//       bindings 9..15 are the kind's own data, uploaded from core::Scene::primitiveData.
//
// Every tracer is compiled once per kind and only calls these, so swapping this file for the
// brick version changes what is inside a primitive without touching any tracer.

import types;
#include "thesis/shared/Bindings.h"

[[vk::binding(THESIS_BINDING_PRIMITIVE_BOUNDS, 0)]]
StructuredBuffer<Aabb> gPrimitiveBounds;

public struct PrimitiveHit
{
    public float t;
    public uint  face;
};

// Slab test. Rays that start inside a box report no hit for that box (cameras are outside).
public bool intersectPrimitive(Ray ray, uint primitiveId, out PrimitiveHit hit)
{
    hit.t = 0.0;
    hit.face = 0;

    Aabb box = gPrimitiveBounds[primitiveId];
    float3 inverseDirection = 1.0 / ray.direction;
    float3 t0 = (box.lo() - ray.origin) * inverseDirection;
    float3 t1 = (box.hi() - ray.origin) * inverseDirection;
    float3 tNear = min(t0, t1);
    float3 tFar = max(t0, t1);
    float tEnter = max(max(tNear.x, tNear.y), tNear.z);
    float tExit = min(min(tFar.x, tFar.y), tFar.z);

    if (tEnter > tExit || tEnter < ray.tMin || tEnter > ray.tMax)
        return false;

    uint axis = (tEnter == tNear.x) ? 0 : ((tEnter == tNear.y) ? 1 : 2);
    hit.t = tEnter;
    hit.face = axis * 2 + (ray.direction[axis] > 0.0 ? 0 : 1);
    return true;
}
