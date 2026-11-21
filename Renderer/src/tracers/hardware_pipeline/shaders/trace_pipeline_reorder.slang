// Hardware pipeline tracers with shader execution reordering (SER,
// VK_NV_ray_tracing_invocation_reorder): the GPU may regroup the rays of a launch between
// shaders, so threads that run next to each other do similar work. Two ray generation shaders,
// each one tracer:
//
//   reorderAfterHit         The trace runs the brick tests in the intersection shader, as in
//                           trace_pipeline.slang. Only then are the rays regrouped, by their hit,
//                           before the closest-hit shader. The control: the brick tests are done
//                           before the regrouping, so it can only help the little work after them.
//
//   reorderBeforeBrickTest  The trace only finds the box the ray enters first (testBox). The rays
//                           are regrouped by that box, then the ray generation shader runs the
//                           brick test itself, so rays in the same brick test it side by side. On
//                           a miss the ray is traced again from where it leaves the box.
//                           WHY it is exact only for boxes that do not overlap: a box that lies
//                           inside the stretch of the ray through another box would be skipped.
//                           The tracer checks the boxes in prepare() and refuses other scenes.
//
// Its own file, because SPIR-V declares the capability for the whole module: the pipeline
// without reordering must not carry it. Compiled once per primitive kind.
// WHY the NV extension and not the cross-vendor EXT one: the Vulkan headers this project builds
// with (vcpkg, header version 309) only know the NV one. The build passes the capability
// spvShaderInvocationReorderNV, so Slang writes the NV instructions (it writes EXT by default).

import types;
import work;
import closest;
import pipeline_stages;

// How many low bits of the hint the GPU may use to regroup. The hint is the index of the box, so
// rays in the same brick get the same hint; bricks whose index differs only above these bits
// share a hint too.
// WHY 16: a first choice, not tuned. It tells apart 65536 neighbouring bricks; whether fewer or
// more bits group better is open, and the driver may use fewer bits than given.
static const uint kHintBits = 16;

[shader("raygeneration")]
void reorderAfterHit()
{
    const uint rayIndex = DispatchRaysIndex().x;
    if (rayIndex >= gParams.rayCount)
        return;
    const Ray ray = gRays[rayIndex];
    beginRay(rayIndex);
    Payload payload;
    payload.hit = makeMiss(ray);
    HitObject hitObject = HitObject.TraceRay(gScene, RAY_FLAG_FORCE_OPAQUE, 0xFF, 0, 0, 0, rayDesc(ray), payload);
    ReorderThread(hitObject);
    HitObject.Invoke(gScene, hitObject, payload);
    endRay(rayIndex, payload.hit);
}

[shader("raygeneration")]
void reorderBeforeBrickTest()
{
    const uint rayIndex = DispatchRaysIndex().x;
    if (rayIndex >= gParams.rayCount)
        return;
    const Ray ray = gRays[rayIndex];
    ClosestHit closest = ClosestHit.none(ray, rayIndex);
    Payload payload; // unused: no closest-hit shader runs
    payload.hit = makeMiss(ray);

    // Front to back through the boxes. Each pass starts where the previous box ends, so the start
    // moves forward strictly (testBox) and the loop ends. It also ends once the start passes the
    // closest hit: the boxes do not overlap, so none farther can hold a closer hit.
    float start = ray.tMin;
    while (start < closest.t)
    {
        RayDesc desc = rayDesc(ray);
        desc.TMin = start;
        desc.TMax = closest.t;
        HitObject box = HitObject.TraceRay(gScene, RAY_FLAG_FORCE_OPAQUE, 0xFF, 0, 0, 0, desc, payload);
        if (!box.IsHit())
            break;
        const uint primitive = box.GetPrimitiveIndex();
        ReorderThread(box, primitive, kHintBits);
        const BoxExit exit = box.GetAttributes<BoxExit>();
        closest.test(ray, primitive);
        start = exit.t;
    }

    gHits[rayIndex] = closest.finish();
    closest.work.store(rayIndex);
}

[shader("intersection")]
void intersectBrick()
{
    testBrick();
}

[shader("intersection")]
void intersectBox()
{
    testBox();
}

[shader("closesthit")]
void closestHit(inout Payload payload, PrimitiveHit hit)
{
    finishHit(payload, hit);
}

// The payload already holds the miss.
[shader("miss")]
void miss(inout Payload payload) {}
