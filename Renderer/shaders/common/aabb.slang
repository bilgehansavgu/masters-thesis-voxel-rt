// ============================================================================
// READING ORDER: FILE 14A
// ============================================================================
// PURPOSE:
// Implements the classic Ray vs. Axis-Aligned Bounding Box (AABB) "Slab Test",
// shared by all primitive kinds (`box`, `brick2012_inline`, `brick2012_split`).
//
// RAY TRACING CONCEPT: THE SLAB TEST
// An axis-aligned box is the intersection of 3 pairs of parallel planes ("slabs"):
//   - X-slab: between x = box.lo().x and x = box.hi().x
//   - Y-slab: between y = box.lo().y and y = box.hi().y
//   - Z-slab: between z = box.lo().z and z = box.hi().z
//
// A ray `P(t) = origin + t * direction` is inside the box if and only if it is
// simultaneously inside all 3 slabs!
//   1. For each axis, we solve `origin + t * direction = box.lo()` and `box.hi()`
//      to get `t0` and `t1`.
//   2. Because the ray direction can be negative, we take `tNear = min(t0, t1)`
//      (where the ray enters each slab) and `tFar = max(t0, t1)` (where it exits).
//   3. The ray enters the 3D box when it has entered ALL 3 slabs:
//      `tEnter = max(tNear.x, tNear.y, tNear.z)`.
//   4. The ray exits the 3D box as soon as it exits ANY of the 3 slabs:
//      `tExit = min(tFar.x, tFar.y, tFar.z)`.
//   5. If `tEnter <= tExit` and `tMin <= tEnter <= tMax`, we have a valid hit!
// ============================================================================

import types;

// Returns true if `ray` enters `box` at a distance t with tMin <= t <= tMax. `face` is the side
// the ray enters through: 0..5 = -X, +X, -Y, +Y, -Z, +Z.
// Rays that start inside the box report no hit for it (cameras are outside).
public bool intersectAabb(Ray ray, Aabb box, out float t, out uint face)
{
    t = 0.0;
    face = 0;

    float3 inverseDirection = 1.0 / ray.direction;
    float3 t0 = (box.lo() - ray.origin) * inverseDirection;
    float3 t1 = (box.hi() - ray.origin) * inverseDirection;
    float3 tNear = min(t0, t1);
    float3 tFar = max(t0, t1);
    float tEnter = max(max(tNear.x, tNear.y), tNear.z);
    float tExit = min(min(tFar.x, tFar.y), tFar.z);

    if (tEnter > tExit || tEnter < ray.tMin || tEnter > ray.tMax)
        return false;

    // Which slab did the ray enter LAST? That slab's plane is the face of the box we hit!
    uint axis = (tEnter == tNear.x) ? 0 : ((tEnter == tNear.y) ? 1 : 2);
    t = tEnter;
    // If ray.direction[axis] > 0, the ray is moving in the +axis direction, so it hit the
    // negative-facing side of the box (-X=0, -Y=2, -Z=4). Otherwise +X=1, +Y=3, +Z=5.
    face = axis * 2 + (ray.direction[axis] > 0.0 ? 0 : 1);
    return true;
}
