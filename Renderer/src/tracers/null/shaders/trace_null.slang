// Null tracer: read each ray, write a miss. The floor every tracer pays for the ray and hit lists.

import types;
#include "thesis/shared/Bindings.h"

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

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 threadId : SV_DispatchThreadID)
{
    uint rayIndex = threadId.x;
    if (rayIndex >= gParams.rayCount)
        return;

    // Only tMax is used, but a Ray is 32 bytes, one GPU memory sector: touching any field moves
    // the same bytes from memory as reading the whole ray, as a real tracer does.
    Ray ray = gRays[rayIndex];
    gHits[rayIndex] = makeMiss(ray);
}
