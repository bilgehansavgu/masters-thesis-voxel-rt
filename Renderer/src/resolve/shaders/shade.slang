// Shading: hit list → image. Shared by every tracer.

import types;
import normals;
import display;
#include "thesis/shared/Bindings.h"
#include "thesis/shared/Hit.h"
#include "thesis/shared/RayOrder.h"

struct ShadeParams
{
    uint width;
    uint height;
    uint mode;  // 0 shaded, 1 distance, 2 primitive id, 3 normal, 4 faces
    uint rayOrder; // THESIS_RAY_ORDER_*: where the hit of a pixel is in the hit list
    float distanceNear;
    float distanceFar;
    float lightAzimuth;   // radians, around the up axis (y), 0 = towards +z
    float lightElevation; // radians above the horizontal plane
};

[[vk::push_constant]] ConstantBuffer<ShadeParams> gParams;

[[vk::binding(THESIS_BINDING_SHADE_HITS, 0)]] StructuredBuffer<Hit> gHits;
[[vk::binding(THESIS_BINDING_SHADE_OUTPUT, 0)]] [[vk::image_format("rgba8")]] RWTexture2D<float4> gOutput;

float3 colorFromId(uint id)
{
    uint h = id * 2654435761u;
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    return float3(float(h & 0xFF), float((h >> 8) & 0xFF), float((h >> 16) & 0xFF)) / 255.0;
}

// The normal of the side of the box or voxel that the ray entered through.
float3 faceNormal(uint face)
{
    float3 normal = float3(0.0, 0.0, 0.0);
    normal[face / 2] = (face & 1) != 0 ? 1.0 : -1.0;
    return normal;
}

float3 lit(float3 base, float3 normal)
{
    const float horizontal = cos(gParams.lightElevation);
    const float3 toLight = float3(horizontal * sin(gParams.lightAzimuth), sin(gParams.lightElevation),
                                  horizontal * cos(gParams.lightAzimuth));
    return base * (0.25 + 0.75 * max(dot(normal, toLight), 0.0));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 threadId : SV_DispatchThreadID)
{
    if (threadId.x >= gParams.width || threadId.y >= gParams.height)
        return;

    Hit hit = gHits[THESIS_RAY_INDEX(gParams.rayOrder, threadId.x, threadId.y, gParams.width, gParams.height)];
    // The normal that the primitive kind stores for the hit point. A kind without one (the box)
    // gets the normal of the face.
    const bool hasStoredNormal = (hit.attributes & THESIS_HIT_HAS_NORMAL) != 0;
    const float3 storedNormal =
        hasStoredNormal ? decodeNormal(hit.attributes & THESIS_HIT_NORMAL_MASK) : faceNormal(hit.face);
    float3 color;

    if (hit.primitive == kNoHit)
    {
        color = backgroundColor(float(threadId.y) / float(gParams.height));
    }
    else if (gParams.mode == 1)
    {
        float d = saturate((hit.t - gParams.distanceNear) / max(gParams.distanceFar - gParams.distanceNear, 1e-6));
        color = float3(1.0 - d, 1.0 - d, 1.0 - d);
    }
    else if (gParams.mode == 2)
    {
        color = colorFromId(hit.primitive);
    }
    else if (gParams.mode == 3)
    {
        color = storedNormal * 0.5 + 0.5;
    }
    else if (gParams.mode == 4)
    {
        color = lit(float3(0.8, 0.8, 0.8), faceNormal(hit.face));
    }
    else
    {
        // A model with stored normals is one surface: one colour. Boxes get a colour each.
        const float3 base = hasStoredNormal ? float3(0.8, 0.8, 0.8) : 0.35 + 0.65 * colorFromId(hit.primitive);
        color = lit(base, storedNormal);
    }

    gOutput[threadId.xy] = toDisplay(color);
}
