// ============================================================================
// READING ORDER: FILE 8 (Stage 3 of every frame: Shading hits into an image)
// ============================================================================
// Shading: hit list -> image. Shared by every tracer.
//
// RAY TRACING BASICS: What is "Shading"?
// Once the tracer has filled `gHits` with the closest hit for every pixel, we need to turn each
// `Hit` into an RGB pixel color so you can see the 3D scene on screen:
//   - If `hit.primitive == kNoHit`, the ray missed everything -> draw a blue-grey sky gradient.
//   - Otherwise, color the pixel according to the user's selected `mode` (keys 1-4 in the window):
//       Mode 0 (Shaded): lit 3D surfaces using the surface normal and a directional sun light.
//       Mode 1 (Distance): greyscale depth map (white = close to camera, black = far away).
//       Mode 2 (Primitive id): flat pseudo-random color per primitive ID (great for debugging).
//       Mode 3 (Normal): surface direction (X, Y, Z) mapped from [-1, +1] to RGB [0, 1].

import types;
#include "thesis/shared/Bindings.h"

struct ShadeParams
{
    uint width;
    uint height;
    uint mode;  // 0 shaded, 1 distance, 2 primitive id, 3 normal
    uint pad0;
    float distanceNear;
    float distanceFar;
    float pad1;
    float pad2;
};

[[vk::push_constant]] ConstantBuffer<ShadeParams> gParams;

// Input: the hit array produced by the active tracer.
[[vk::binding(THESIS_BINDING_SHADE_HITS, 0)]] StructuredBuffer<Hit> gHits;
// Output: 2D RGBA image (8 bits per channel) that will be copied to the window.
[[vk::binding(THESIS_BINDING_SHADE_OUTPUT, 0)]] [[vk::image_format("rgba8")]] RWTexture2D<float4> gOutput;

// Turns an integer primitive ID (0, 1, 2, ...) into a distinct, repeatable RGB color in [0, 1].
// Uses an integer hash (bit-mixing) so neighboring IDs (like 1 and 2) get very different colors.
float3 colorFromId(uint id)
{
    uint h = id * 2654435761u;
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    return float3(float(h & 0xFF), float((h >> 8) & 0xFF), float((h >> 16) & 0xFF)) / 255.0;
}

// Reconstructs the 3D outward surface normal vector from the compact `face` index (0..5):
//   face 0 -> (-1,  0,  0)   face 1 -> (+1,  0,  0)   [face / 2 == 0 -> X axis]
//   face 2 -> ( 0, -1,  0)   face 3 -> ( 0, +1,  0)   [face / 2 == 1 -> Y axis]
//   face 4 -> ( 0,  0, -1)   face 5 -> ( 0,  0, +1)   [face / 2 == 2 -> Z axis]
// Even numbers have `(face & 1) == 0` -> -1.0; odd numbers have `(face & 1) != 0` -> +1.0.
float3 faceNormal(uint face)
{
    float3 normal = float3(0.0, 0.0, 0.0);
    normal[face / 2] = (face & 1) != 0 ? 1.0 : -1.0;
    return normal;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 threadId : SV_DispatchThreadID)
{
    if (threadId.x >= gParams.width || threadId.y >= gParams.height)
        return;

    Hit hit = gHits[threadId.y * gParams.width + threadId.x];
    float3 color;

    if (hit.primitive == kNoHit)
    {
        // Sky background: vertical gradient (`lerp` = linear interpolation from top to bottom).
        float v = float(threadId.y) / float(gParams.height);
        color = lerp(float3(0.55, 0.70, 0.90), float3(0.22, 0.27, 0.36), v);
    }
    else if (gParams.mode == 1)
    {
        // Distance view: normalize `hit.t` into [0, 1] between `distanceNear` and `distanceFar`.
        // `saturate(x)` clamps `x` to [0.0, 1.0]. Invert (`1.0 - d`) so near objects are bright.
        float d = saturate((hit.t - gParams.distanceNear) / max(gParams.distanceFar - gParams.distanceNear, 1e-6));
        color = float3(1.0 - d, 1.0 - d, 1.0 - d);
    }
    else if (gParams.mode == 2)
    {
        // Primitive ID view: unlit flat color per object.
        color = colorFromId(hit.primitive);
    }
    else if (gParams.mode == 3)
    {
        // Normal view: shift normal components from [-1, +1] into visible RGB range [0, 1].
        color = faceNormal(hit.face) * 0.5 + 0.5;
    }
    else
    {
        // Mode 0 (Shaded): Lambertian (diffuse) lighting + ambient light.
        // `dot(N, L)` is the cosine of the angle between the surface normal and the light direction:
        //   1.0 when the surface faces the sun directly, 0.0 when edge-on or facing away.
        // `0.25` is ambient light so shadowed faces are still visible instead of pitch black.
        float3 base = 0.35 + 0.65 * colorFromId(hit.primitive);
        float3 toLight = normalize(float3(0.4, 1.0, 0.3));
        float diffuse = max(dot(faceNormal(hit.face), toLight), 0.0);
        color = base * (0.25 + 0.75 * diffuse);
    }

    // Apply standard sRGB gamma encoding (`pow(color, 1 / 2.2)`) so linear light values look
    // natural on a monitor, and write the final RGBA pixel to the output image.
    gOutput[threadId.xy] = float4(pow(saturate(color), 1.0 / 2.2), 1.0);
}
