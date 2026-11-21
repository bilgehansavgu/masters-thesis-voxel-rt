// What every ray tracing pipeline of the hardware pipeline tracers shares: the resources, the
// payload, and the work of the intersection and closest-hit shaders. The entry points themselves
// are in trace_pipeline.slang and trace_pipeline_reorder.slang, because Slang only compiles entry
// points of the file it is given.
//
// One ray runs in several shaders here, one after the other on the same thread: ray generation,
// then the intersection shader for every box the GPU's BVH walk reaches, then the closest-hit
// shader (or the miss shader). The brick test is the same ClosestHit.test that every tracer runs.

import types;
import work;
import closest;
import aabb;
import bounds;
#include "thesis/shared/Bindings.h"
#include "thesis/tracers/hardware_pipeline/HardwarePipelineShared.h"

public struct TraceParams
{
    public uint rayCount;
    public uint pad0;
    public uint pad1;
    public uint pad2;
};

[[vk::push_constant]] public ConstantBuffer<TraceParams> gParams;

[[vk::binding(THESIS_BINDING_RAYS, 0)]] public StructuredBuffer<Ray> gRays;
[[vk::binding(THESIS_BINDING_HITS, 0)]] public RWStructuredBuffer<Hit> gHits;
[[vk::binding(THESIS_BINDING_HARDWARE_PIPELINE_SCENE, 0)]] public RaytracingAccelerationStructure gScene;

// What travels with one ray between its shaders: the hit, a miss until the closest-hit shader
// writes it.
public struct Payload
{
    public Hit hit;
};

// What the box-only intersection shader reports with a box: where the ray leaves it.
public struct BoxExit
{
    public float t;
};

public RayDesc rayDesc(Ray ray)
{
    RayDesc desc;
    desc.Origin = ray.origin;
    desc.TMin = ray.tMin;
    desc.Direction = ray.direction;
    desc.TMax = ray.tMax;
    return desc;
}

// The ray as an intersection shader sees it: tMax is the closest hit so far.
public Ray currentRay()
{
    Ray ray;
    ray.origin = WorldRayOrigin();
    ray.direction = WorldRayDirection();
    ray.tMin = RayTMin();
    ray.tMax = RayTCurrent();
    return ray;
}

// Ray generation, before the trace: the work count of the ray starts (counting variant only).
public void beginRay(uint rayIndex)
{
    WorkCounter work = WorkCounter.start(rayIndex);
    work.store(rayIndex);
}

// Ray generation, after the trace. A hit has its result logged by the closest-hit shader; a miss
// has it logged here.
public void endRay(uint rayIndex, Hit hit)
{
    gHits[rayIndex] = hit;
    if (hit.primitive == kNoHit)
    {
        WorkCounter work = WorkCounter.resume(rayIndex);
        work.result(hit);
        work.store(rayIndex);
    }
}

// Intersection shader: the brick test of the box the BVH walk reached. A hit closer than the
// closest so far is reported; the hardware keeps the closest one.
public void testBrick()
{
    const uint rayIndex = DispatchRaysIndex().x;
    const Ray ray = currentRay();
    ClosestHit closest = ClosestHit.resume(ray, rayIndex);
    if (closest.test(ray, PrimitiveIndex()))
        ReportHit(closest.t, 0, closest.primitiveHit());
    closest.work.store(rayIndex);
}

// Intersection shader: only the box, no brick test. Reports where the ray enters the box, so the
// hardware keeps the box the ray enters first, and where it leaves it.
// WHY exit > tMin, strictly: the next trace of the ray starts at the exit of the box it just
// tested; that box must not come back, or the walk would not move on.
public void testBox()
{
    Ray ray = currentRay();
    ray.tMax = kNever; // the exit may lie beyond the closest box so far
    const Aabb box = gPrimitiveBounds[PrimitiveIndex()];
    float enter;
    float exit;
    if (clipRayToBox(ray, box.lo(), box.hi(), enter, exit) && exit > ray.tMin)
    {
        BoxExit attributes;
        attributes.t = exit;
        ReportHit(enter, 0, attributes); // ignored by the hardware if enter > RayTCurrent()
    }
}

// Closest-hit shader: reads the attributes of the closest hit, once for each ray.
public void finishHit(inout Payload payload, PrimitiveHit hit)
{
    const uint rayIndex = DispatchRaysIndex().x;
    ClosestHit closest = ClosestHit.found(PrimitiveIndex(), hit, rayIndex);
    payload.hit = closest.finish();
    closest.work.store(rayIndex);
}
