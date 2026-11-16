// ============================================================================
// READING ORDER: FILE 13
// ============================================================================
// Ray workload: one primary ray per pixel from a pinhole camera, written row-major.
//
// RAY TRACING BASICS: How does a Pinhole Camera work?
// Imagine looking out through a screen door where each tiny square is one pixel of your image:
//   - Your eye is at `origin`.
//   - The center of the screen is at `origin + forward`.
//   - Moving across the screen horizontally adds multiples of `right` (from -1 on the left edge
//     to +1 on the right edge).
//   - Moving across the screen vertically adds multiples of `up` (from +1 on the top edge
//     to -1 on the bottom edge).
// For each pixel (x, y), we shoot a ray starting at `origin` and pointing through the center
// of that pixel's square on the virtual screen.

import types;
#include "thesis/shared/Bindings.h"

// Small parameter block sent directly inside the Vulkan command buffer ("push constants")
// so we do not need to allocate a GPU buffer just for 80 bytes of camera vectors.
struct CameraParams
{
    float4 origin;   // .xyz = camera eye position in world space
    float4 forward;  // .xyz = unit vector pointing straight ahead from the camera
    float4 right;    // .xyz = horizontal screen vector, pre-scaled by tan(fov/2) * aspectRatio
    float4 up;       // .xyz = vertical screen vector, pre-scaled by tan(fov/2)
    uint width;      // image width in pixels (e.g., 1920)
    uint height;     // image height in pixels (e.g., 1080)
    uint pad0;
    uint pad1;
};

[[vk::push_constant]] ConstantBuffer<CameraParams> gCamera;

// Output buffer at binding slot 0 (`RWStructuredBuffer` = Read/Write array of `Ray` structs).
[[vk::binding(THESIS_BINDING_RAYS, 0)]] RWStructuredBuffer<Ray> gRays;

// GPU BASICS: How does a Compute Shader run?
// The GPU executes `main` for millions of threads in parallel, grouped into small tiles
// called "workgroups" or "thread groups". Here `[numthreads(8, 8, 1)]` means each workgroup
// is an 8x8 tile of 64 threads.
// `threadId` (`SV_DispatchThreadID`) gives this thread's global 2D pixel coordinate `(x, y)`.
[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 threadId : SV_DispatchThreadID)
{
    // If the image size is not a multiple of 8, edge workgroups have extra threads outside the
    // image bounds; those threads exit immediately.
    if (threadId.x >= gCamera.width || threadId.y >= gCamera.height)
        return;

    // Step 1: Aim through the exact center of the pixel (+0.5, +0.5), not its top-left corner.
    float2 pixelCenter = float2(threadId.xy) + 0.5;

    // Step 2: Convert pixel coordinates [0..width, 0..height] to Normalized Device Coordinates
    // (NDC) in [-1, +1]. Note that pixel y=0 is the TOP of the screen, so `ndc.y` is -1 at the
    // top and +1 at the bottom (y grows downwards).
    float2 ndc = pixelCenter / float2(gCamera.width, gCamera.height) * 2.0 - 1.0;

    // Step 3: Build the ray for this pixel.
    // Because `ndc.y` grows downwards while `gCamera.up` points upwards in 3D world space,
    // we subtract `ndc.y * gCamera.up.xyz`. `normalize(...)` scales the direction vector to length 1.
    Ray ray;
    ray.origin = gCamera.origin.xyz;
    ray.tMin = 0.0;
    ray.direction = normalize(gCamera.forward.xyz + ndc.x * gCamera.right.xyz - ndc.y * gCamera.up.xyz);
    ray.tMax = 1.0e30; // effectively infinity

    // Step 4: Flatten the 2D pixel coordinate (x, y) into a 1D array index (row-major order):
    // row `y` skips `y * width` elements, plus column `x`.
    gRays[threadId.y * gCamera.width + threadId.x] = ray;
}
