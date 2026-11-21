// Hardware tracer: the ray tracing cores walk the GPU-built BVH and hand us every primitive box
// the ray touches; we answer with the same primitive test the software tracer uses.
// Compiled once per primitive kind; `primitive` resolves to shaders/primitives/<kind>/.

import types;
import closest;
#include "thesis/shared/Bindings.h"
#include "thesis/tracers/hardware/HardwareShared.h"

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
    ClosestHit closest = ClosestHit.none(ray, rayIndex);

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
            // Tells the hardware to skip everything farther than this hit.
            if (closest.test(ray, query.CandidatePrimitiveIndex()))
                query.CommitProceduralPrimitiveHit(closest.t);
        }
    }

    gHits[rayIndex] = closest.finish();
    closest.work.store(rayIndex);
}
