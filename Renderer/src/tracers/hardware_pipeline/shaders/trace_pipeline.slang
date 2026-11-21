// Hardware pipeline tracer: the same search as the hardware tracer (the GPU builds and walks the
// BVH), but as a ray tracing pipeline instead of a ray query in a compute shader. The brick test
// runs in an intersection shader, which the GPU calls for every box its walk reaches.
// Compiled once per primitive kind; `primitive` resolves to shaders/primitives/<kind>/.

import types;
import closest;
import pipeline_stages;

[shader("raygeneration")]
void rayGeneration()
{
    const uint rayIndex = DispatchRaysIndex().x;
    if (rayIndex >= gParams.rayCount)
        return;
    const Ray ray = gRays[rayIndex];
    beginRay(rayIndex);
    Payload payload;
    payload.hit = makeMiss(ray);
    TraceRay(gScene, RAY_FLAG_FORCE_OPAQUE, 0xFF, 0, 0, 0, rayDesc(ray), payload);
    endRay(rayIndex, payload.hit);
}

[shader("intersection")]
void intersectBrick()
{
    testBrick();
}

[shader("closesthit")]
void closestHit(inout Payload payload, PrimitiveHit hit)
{
    finishHit(payload, hit);
}

// The payload already holds the miss.
[shader("miss")]
void miss(inout Payload payload) {}
