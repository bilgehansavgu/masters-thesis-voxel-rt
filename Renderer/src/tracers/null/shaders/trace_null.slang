// ============================================================================
// READING ORDER: FILE 5 (Stage 2A: The "Null" Baseline Tracer Shader)
// ============================================================================
// Null tracer: read each ray, write a miss. The floor every tracer pays for the ray and hit lists.
//
// WHY DOES THIS EXIST?
// Every tracer in our testbed must read 2 million `Ray` structs from GPU memory (slot 0) and
// write 2 million `Hit` structs back to GPU memory (slot 1). That memory traffic alone takes
// ~0.43 ms on an RTX 4070 Laptop GPU!
// By measuring a shader that ONLY reads the ray and writes a miss (without testing any geometry),
// we can subtract this fixed memory cost (`tracer_ms - null_ms`) to see how long the actual
// ray-scene search took.

import types;
#include "thesis/shared/Bindings.h"

struct TraceParams
{
    uint rayCount;  // total number of rays in gRays (width * height)
    uint pad0;
    uint pad1;
    uint pad2;
};

[[vk::push_constant]] ConstantBuffer<TraceParams> gParams;

// Slot 0: read-only input ray list. Slot 1: read-write output hit list.
[[vk::binding(THESIS_BINDING_RAYS, 0)]] StructuredBuffer<Ray> gRays;
[[vk::binding(THESIS_BINDING_HITS, 0)]] RWStructuredBuffer<Hit> gHits;

// 1D compute dispatch: 64 threads per workgroup (`threadId.x` is the 1D ray index).
[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 threadId : SV_DispatchThreadID)
{
    uint rayIndex = threadId.x;
    if (rayIndex >= gParams.rayCount)
        return;

    // Only tMax is used by makeMiss(), but a Ray is 32 bytes (one GPU memory sector): touching
    // any field transfers the same 32-byte sector from GPU memory as reading the whole ray.
    Ray ray = gRays[rayIndex];
    gHits[rayIndex] = makeMiss(ray);
}
