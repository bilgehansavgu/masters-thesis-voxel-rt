// The closest hit that one ray has found so far. Every tracer that searches uses it, so all of
// them treat a candidate primitive the same way and read the attributes of the hit exactly once.
//
// `primitive` resolves to shaders/primitives/<kind>/, so this module is compiled per kind too.

import types;
import work;
import primitive;

public struct ClosestHit
{
    public float t;         // distance of the closest hit; the ray's tMax while there is none
    public uint  primitive; // kNoHit while there is none
    PrimitiveHit hit;
    public WorkCounter work;

    // `rayIndex` is the index of the ray in the ray list. The counting variant needs it to know
    // whether this is the ray of the ray log.
    public static ClosestHit none(Ray ray, uint rayIndex)
    {
        ClosestHit closest;
        closest.t = ray.tMax;
        closest.primitive = kNoHit;
        closest.hit.t = ray.tMax;
        closest.hit.face = 0;
        closest.hit.detail = 0;
        closest.work = WorkCounter.start(rayIndex);
        return closest;
    }

    // For a shader of a ray tracing pipeline that runs in the middle of a ray (an intersection
    // shader): the closest hit so far is only known as a distance, the ray's tMax (RayTCurrent),
    // and the work so far is in the work list (WorkCounter.resume).
    public static ClosestHit resume(Ray ray, uint rayIndex)
    {
        ClosestHit closest = none(ray, rayIndex);
        closest.work = WorkCounter.resume(rayIndex);
        return closest;
    }

    // The hit that an earlier shader of the same ray found and the hardware kept as the closest
    // (in a ray tracing pipeline's closest-hit shader). finish() then reads its attributes.
    public static ClosestHit found(uint primitiveId, PrimitiveHit hit, uint rayIndex)
    {
        ClosestHit closest;
        closest.t = hit.t;
        closest.primitive = primitiveId;
        closest.hit = hit;
        closest.work = WorkCounter.resume(rayIndex);
        return closest;
    }

    // What the primitive kind reported for the closest hit (valid when primitive != kNoHit).
    public PrimitiveHit primitiveHit() { return hit; }

    // Tests the ray against one primitive. Returns true if that primitive is now the closest hit.
    // The test ends at the closest hit so far: nothing behind it can matter.
    [mutating] public bool test(Ray ray, uint primitiveId)
    {
        work.primitiveTest(primitiveId);
        Ray shortened = ray;
        shortened.tMax = t;
        PrimitiveHit candidate;
        if (!intersectPrimitive(shortened, primitiveId, candidate, work) || candidate.t >= t)
            return false;
        t = candidate.t;
        primitive = primitiveId;
        work.closerHit(primitiveId, t);
        hit = candidate;
        return true;
    }

    // The entry for the hit list. Reads the attributes of the hit point: one read for each ray.
    [mutating] public Hit finish()
    {
        Hit result;
        result.t = t;
        result.primitive = primitive;
        result.face = 0;
        result.attributes = 0;
        if (primitive != kNoHit)
        {
            result.face = hit.face;
            result.attributes = primitiveAttributes(primitive, hit, work);
        }
        work.result(result);
        return result;
    }
};
