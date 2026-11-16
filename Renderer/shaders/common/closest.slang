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

    public static ClosestHit none(Ray ray)
    {
        ClosestHit closest;
        closest.t = ray.tMax;
        closest.primitive = kNoHit;
        closest.hit.t = ray.tMax;
        closest.hit.face = 0;
        closest.hit.detail = 0;
        closest.work = WorkCounter.start();
        return closest;
    }

    // Tests the ray against one primitive. Returns true if that primitive is now the closest hit.
    // The test ends at the closest hit so far: nothing behind it can matter.
    [mutating] public bool test(Ray ray, uint primitiveId)
    {
        work.primitiveTest();
        Ray shortened = ray;
        shortened.tMax = t;
        PrimitiveHit candidate;
        if (!intersectPrimitive(shortened, primitiveId, candidate, work) || candidate.t >= t)
            return false;
        t = candidate.t;
        primitive = primitiveId;
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
        return result;
    }
};
