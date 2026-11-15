// Ray against axis-aligned box: the slab test. Shared by every primitive kind.

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
