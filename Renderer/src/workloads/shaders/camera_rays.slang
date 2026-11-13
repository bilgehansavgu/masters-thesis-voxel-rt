// Ray workload: one primary ray per pixel from a pinhole camera, written row-major.

import types;
#include "thesis/shared/Bindings.h"

struct CameraParams
{
    float4 origin;
    float4 forward;
    float4 right;  // scaled: the image plane at distance 1 spans [-1, 1]
    float4 up;     // scaled likewise
    uint width;
    uint height;
    uint pad0;
    uint pad1;
};

[[vk::push_constant]] ConstantBuffer<CameraParams> gCamera;

[[vk::binding(THESIS_BINDING_RAYS, 0)]] RWStructuredBuffer<Ray> gRays;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 threadId : SV_DispatchThreadID)
{
    if (threadId.x >= gCamera.width || threadId.y >= gCamera.height)
        return;

    float2 pixelCenter = float2(threadId.xy) + 0.5;
    float2 ndc = pixelCenter / float2(gCamera.width, gCamera.height) * 2.0 - 1.0;  // y grows downwards

    Ray ray;
    ray.origin = gCamera.origin.xyz;
    ray.tMin = 0.0;
    ray.direction = normalize(gCamera.forward.xyz + ndc.x * gCamera.right.xyz - ndc.y * gCamera.up.xyz);
    ray.tMax = 1.0e30;
    gRays[threadId.y * gCamera.width + threadId.x] = ray;
}
