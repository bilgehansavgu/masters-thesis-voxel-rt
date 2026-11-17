// Ray against axis-aligned box: the slab test. Shared by the primitive kinds and the tracers.

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

    uint axis = (tEnter == tNear.x) ? 0 : ((tEnter == tNear.y) ? 1 : 2);
    t = tEnter;
    face = axis * 2 + (ray.direction[axis] > 0.0 ? 0 : 1);
    return true;
}

// The part of the ray inside the box from `lo` to `hi`: from tEnter to tExit, inside the limits
// tMin and tMax of the ray. Returns false if there is none. A ray that starts inside the box
// gets tEnter = tMin. A direction component of zero is a case of its own, because a division
// gives no usable number when the origin lies on a side of the box.
public bool clipRayToBox(Ray ray, float3 lo, float3 hi, out float tEnter, out float tExit)
{
    tEnter = ray.tMin;
    tExit = ray.tMax;
    for (uint axis = 0; axis < 3; ++axis)
    {
        if (ray.direction[axis] == 0.0)
        {
            if (ray.origin[axis] < lo[axis] || ray.origin[axis] > hi[axis])
                return false;
            continue;
        }
        const float tA = (lo[axis] - ray.origin[axis]) / ray.direction[axis];
        const float tB = (hi[axis] - ray.origin[axis]) / ray.direction[axis];
        tEnter = max(tEnter, min(tA, tB));
        tExit = min(tExit, max(tA, tB));
    }
    return tEnter <= tExit;
}

// 1 / direction for box tests that run many times for one ray. A component of zero becomes a
// very small number, so the result is very large and finite. The products in boxEntry then
// never give "not a number".
public float3 safeInverse(float3 direction)
{
    const float tiny = 1.0e-20;
    return float3(1.0 / (abs(direction.x) < tiny ? tiny : direction.x),
                  1.0 / (abs(direction.y) < tiny ? tiny : direction.y),
                  1.0 / (abs(direction.z) < tiny ? tiny : direction.z));
}

// The distance at which the ray enters the box from `lo` to `hi`, or kNever if it misses the
// box between tMin and tMax. A ray that starts inside the box gets tMin.
// `inverseDirection` comes from safeInverse.
public float boxEntry(float3 origin, float3 inverseDirection, float3 lo, float3 hi, float tMin, float tMax)
{
    const float3 t0 = (lo - origin) * inverseDirection;
    const float3 t1 = (hi - origin) * inverseDirection;
    const float3 tNear = min(t0, t1);
    const float3 tFar = max(t0, t1);
    const float tEnter = max(max(max(tNear.x, tNear.y), tNear.z), tMin);
    const float tExit = min(min(min(tFar.x, tFar.y), tFar.z), tMax);
    return tEnter <= tExit ? tEnter : kNever;
}
