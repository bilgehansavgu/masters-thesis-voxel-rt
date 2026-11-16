// ============================================================================
// READING ORDER: FILE 17
// ============================================================================
// Hardware tracer: the ray tracing cores walk the GPU-built BVH and hand us every primitive box
// the ray touches; we answer with the same intersectPrimitive() the software tracer uses.
// Compiled once per primitive kind; `import primitive;` resolves to `shaders/primitives/<kind>/`.
//
// RAY TRACING BASICS: How does Hardware Ray Tracing (`RayQuery`) work for custom primitives?
// Modern GPUs (like RTX 4070) have dedicated hardware units (RT cores) that walk a Bounding
// Volume Hierarchy (BVH: a tree of boxes inside larger boxes) much faster than a loop over all
// objects.
//
// However, the RT cores only know how to intersect two things in hardware:
//   1. Triangles (not used in this thesis)
//   2. Axis-Aligned Bounding Boxes (AABBs) enclosing custom "procedural" primitives (like our
//      voxel bricks).
//
// When we build the BVH out of AABBs (`VK_GEOMETRY_TYPE_AABBS_KHR`), the RT hardware does NOT
// automatically assume hitting a bounding box means hitting the object inside it (because a
// voxel brick is mostly empty air inside its box!).
// Instead, `query.Proceed()` walks the BVH in hardware and pauses whenever the ray touches one
// of our AABBs (`CANDIDATE_PROCEDURAL_PRIMITIVE`). Our shader code then runs `intersectPrimitive`
// to check what is inside that specific box, and if we hit something, we call
// `query.CommitProceduralPrimitiveHit(candidate.t)` to shrink the ray's `TMax` in the hardware
// traversal so the RT cores can skip any boxes farther away!

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

// Slot 2: The Top-Level Acceleration Structure (TLAS) built on the GPU in HardwareTracer.cpp.
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

    // Copy our Ray into Slang/DXR/Vulkan's built-in `RayDesc` structure for hardware traversal.
    RayDesc desc;
    desc.Origin = ray.origin;
    desc.TMin = ray.tMin;
    desc.Direction = ray.direction;
    desc.TMax = ray.tMax;

    // Initialize an inline hardware ray query against the BVH (`gScene`).
    // ` 0xFF` is the 8-bit visibility mask (matches `instance.mask = 0xFF` in HardwareTracer.cpp).
    RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
    query.TraceRayInline(gScene, RAY_FLAG_NONE, 0xFF, desc);

    // `query.Proceed()` steps the hardware BVH traversal until the next candidate box is reached,
    // or returns `false` when traversal is completely finished.
    while (query.Proceed())
    {
        if (query.CandidateType() == CANDIDATE_PROCEDURAL_PRIMITIVE)
        {
            // Ask the hardware which primitive's bounding box the ray just touched.
            uint primitiveId = query.CandidatePrimitiveIndex();

            // Run the exact same primitive intersection function that `trace_software.slang` uses.
            PrimitiveHit candidate;
            if (intersectPrimitive(ray, primitiveId, candidate) && candidate.t < best.t)
            {
                best.t = candidate.t;
                best.primitive = primitiveId;
                best.face = candidate.face;

                // Tell the RT hardware that we found a real hit at distance `candidate.t`.
                // This shortens the ray's active TMax inside the hardware BVH walker so it skips
                // all BVH nodes farther than `candidate.t`.
                query.CommitProceduralPrimitiveHit(candidate.t);
            }
        }
    }

    gHits[rayIndex] = best;
}
