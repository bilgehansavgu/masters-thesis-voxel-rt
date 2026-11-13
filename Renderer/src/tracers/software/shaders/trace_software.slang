// Software tracer, step 0: test every ray against every primitive.
// Compiled once per primitive kind; `primitive` resolves to shaders/primitives/<kind>/.

import types;
#include "thesis/shared/Bindings.h"
import primitive;

struct TraceParams
{
    uint rayCount;
    uint primitiveCount;
    uint pad0;
    uint pad1;
};

[[vk::push_constant]] ConstantBuffer<TraceParams> gParams;

[[vk::binding(THESIS_BINDING_RAYS, 0)]] StructuredBuffer<Ray> gRays;
[[vk::binding(THESIS_BINDING_HITS, 0)]] RWStructuredBuffer<Hit> gHits;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 threadId : SV_DispatchThreadID)
{
    uint rayIndex = threadId.x;
    if (rayIndex >= gParams.rayCount)
        return;

    Ray ray = gRays[rayIndex];
    Hit best = makeMiss(ray);

    for (uint primitiveId = 0; primitiveId < gParams.primitiveCount; ++primitiveId)
    {
        PrimitiveHit candidate;
        if (intersectPrimitive(ray, primitiveId, candidate) && candidate.t < best.t)
        {
            best.t = candidate.t;
            best.primitive = primitiveId;
            best.face = candidate.face;
        }
    }

    gHits[rayIndex] = best;
}
