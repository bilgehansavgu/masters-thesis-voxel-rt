// Hardware tracer: the ray tracing cores walk the GPU-built BVH and hand us every primitive box
// the ray touches; we answer with the same intersectPrimitive() the software tracer uses.
// Compiled once per primitive kind; `primitive` resolves to shaders/primitives/<kind>/.

import types;
#include "thesis/shared/Bindings.h"
import primitive;

struct TraceParams
{
    uint rayCount;
    uint pad0;
    uint pad1;
    uint pad2;
};

[[vk::push_constant]] ConstantBuffer<TraceParams> gParams;

[[vk::binding(THESIS_BINDING_RAYS, 0)]] StructuredBuffer<Ray> gRays;
[[vk::binding(THESIS_BINDING_HITS, 0)]] RWStructuredBuffer<Hit> gHits;
[[vk::binding(THESIS_BINDING_HARDWARE_SCENE, 0)]] RaytracingAccelerationStructure gScene;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 threadId : SV_DispatchThreadID)
{
    uint rayIndex = threadId.x;
    if (rayIndex >= gParams.rayCount)
        return;

    Ray ray = gRays[rayIndex];
    Hit best = makeMiss(ray);

    RayDesc desc;
    desc.Origin = ray.origin;
    desc.TMin = ray.tMin;
    desc.Direction = ray.direction;
    desc.TMax = ray.tMax;

    RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
    query.TraceRayInline(gScene, RAY_FLAG_NONE, 0xFF, desc);

    while (query.Proceed())
    {
        if (query.CandidateType() == CANDIDATE_PROCEDURAL_PRIMITIVE)
        {
            uint primitiveId = query.CandidatePrimitiveIndex();
            PrimitiveHit candidate;
            if (intersectPrimitive(ray, primitiveId, candidate) && candidate.t < best.t)
            {
                best.t = candidate.t;
                best.primitive = primitiveId;
                best.face = candidate.face;
                // Tells the hardware to skip everything farther than this hit.
                query.CommitProceduralPrimitiveHit(candidate.t);
            }
        }
    }

    gHits[rayIndex] = best;
}
