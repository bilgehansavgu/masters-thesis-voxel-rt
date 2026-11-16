// ============================================================================
// READING ORDER: FILE 16
// ============================================================================
// Software tracer, step 0: test every ray against every primitive.
// Compiled once per primitive kind; `import primitive;` resolves to `shaders/primitives/<kind>/`.
//
// RAY TRACING BASICS: Finding the Closest Hit
// A ray might pass through multiple objects one behind another (for example, a front box at
// distance t = 3.0 and a back box at distance t = 8.0). Light cannot pass through solid objects,
// so the camera only sees the CLOSEST hit (the smallest valid `t`).
//
// In Step 0, this shader uses a brute-force loop over all `primitiveCount` primitives.
// In later steps, this loop will be replaced by a software spatial search structure (a grid or
// octree) that skips empty space and only calls `intersectPrimitive()` on bricks along the ray.

import types;
#include "thesis/shared/Bindings.h"
import primitive;

struct TraceParams
{
    uint rayCount;        // total number of rays in gRays
    uint primitiveCount;  // total number of primitives in the scene
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

    // Start by assuming the ray misses everything (`best.t` starts at `ray.tMax`, `best.primitive`
    // starts at `kNoHit`).
    Hit best = makeMiss(ray);

    // Loop over every primitive in the scene and keep the hit with the smallest distance `t`.
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

    // Write the closest hit (or the initial miss if nothing was hit) to the output buffer.
    gHits[rayIndex] = best;
}
