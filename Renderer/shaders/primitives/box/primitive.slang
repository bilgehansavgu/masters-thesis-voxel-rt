// ============================================================================
// READING ORDER: FILE 4 (How a ray tests a single 3D box — the "Slab Test")
// ============================================================================
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

// Binding slot 8 holds the array of all primitive bounding boxes in the scene.
[[vk::binding(THESIS_BINDING_PRIMITIVE_BOUNDS, 0)]]
StructuredBuffer<Aabb> gPrimitiveBounds;

public struct PrimitiveHit
{
    public float t;     // distance along the ray where it entered the primitive
    public uint  face;  // 0..5 = -X, +X, -Y, +Y, -Z, +Z
};

// RAY TRACING MATH: The "Slab Test" (Ray vs. Axis-Aligned Bounding Box)
//
// Think of an axis-aligned 3D box as the intersection of 3 infinite "slabs":
//   - X-slab: the space between the two vertical walls   x = box.lo.x  and  x = box.hi.x
//   - Y-slab: the space between the floor and ceiling    y = box.lo.y  and  y = box.hi.y
//   - Z-slab: the space between the back and front walls z = box.lo.z  and  z = box.hi.z
//
// A point on the ray is inside the 3D box ONLY when it is inside all 3 slabs at the same time!
//
// For each axis (X, Y, Z), we solve for `t` where `origin + t * direction` crosses the two walls:
//     origin + t0 * direction = box.lo   ==>   t0 = (box.lo - origin) / direction
//     origin + t1 * direction = box.hi   ==>   t1 = (box.hi - origin) / direction
//
// Because the ray might travel in a negative direction (e.g., right-to-left), `t0` could be
// larger than `t1`. So for each axis:
//   - `tNear = min(t0, t1)` is when the ray ENTERS that axis's slab.
//   - `tFar  = max(t0, t1)` is when the ray EXITS that axis's slab.
//
// Now combine all 3 axes:
//   - The ray has entered ALL 3 slabs only after the LAST entry:
//         tEnter = max(tNear.x, tNear.y, tNear.z)
//   - The ray leaves the box as soon as it exits the FIRST slab:
//         tExit  = min(tFar.x,  tFar.y,  tFar.z)
//
// If `tEnter <= tExit`, the three intervals overlap and the ray hits the box at `t = tEnter`!
// If `tEnter > tExit`, the ray missed at least one slab before entering another.
// Rays that start inside a box have `tEnter < 0 <= ray.tMin`, so they report no hit for that box.
public bool intersectPrimitive(Ray ray, uint primitiveId, out PrimitiveHit hit)
{
    hit.t = 0.0;
    hit.face = 0;

    Aabb box = gPrimitiveBounds[primitiveId];

    // Multiply by `1.0 / ray.direction` instead of dividing twice (faster on GPUs).
    float3 inverseDirection = 1.0 / ray.direction;
    float3 t0 = (box.lo() - ray.origin) * inverseDirection;
    float3 t1 = (box.hi() - ray.origin) * inverseDirection;

    // Entry and exit `t` for each of the 3 slabs (X, Y, Z) in parallel using float3 vector math.
    float3 tNear = min(t0, t1);
    float3 tFar = max(t0, t1);

    // Overall box entry is the latest slab entry; overall box exit is the earliest slab exit.
    float tEnter = max(max(tNear.x, tNear.y), tNear.z);
    float tExit = min(min(tFar.x, tFar.y), tFar.z);

    // Reject if the ray missed the box (`tEnter > tExit`) or if the hit is outside [tMin, tMax].
    if (tEnter > tExit || tEnter < ray.tMin || tEnter > ray.tMax)
        return false;

    // Which of the 6 box faces did the ray enter through?
    // Whichever slab (0 = X, 1 = Y, 2 = Z) produced `tEnter` is the wall the ray crossed last:
    //   - If `ray.direction[axis] > 0`, the ray was moving in the +axis direction, so it hit the
    //     lower wall facing `-axis` (even face index: 0 = -X, 2 = -Y, 4 = -Z).
    //   - If `ray.direction[axis] < 0`, the ray was moving in the -axis direction, so it hit the
    //     upper wall facing `+axis` (odd face index: 1 = +X, 3 = +Y, 5 = +Z).
    uint axis = (tEnter == tNear.x) ? 0 : ((tEnter == tNear.y) ? 1 : 2);
    hit.t = tEnter;
    hit.face = axis * 2 + (ray.direction[axis] > 0.0 ? 0 : 1);
    return true;
}
