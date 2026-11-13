// Shading: hit list → image. Shared by every tracer.

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
        float v = float(threadId.y) / float(gParams.height);
        color = lerp(float3(0.55, 0.70, 0.90), float3(0.22, 0.27, 0.36), v);
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
        color = faceNormal(hit.face) * 0.5 + 0.5;
    }
    else
    {
        float3 base = 0.35 + 0.65 * colorFromId(hit.primitive);
        float3 toLight = normalize(float3(0.4, 1.0, 0.3));
        float diffuse = max(dot(faceNormal(hit.face), toLight), 0.0);
        color = base * (0.25 + 0.75 * diffuse);
    }

    gOutput[threadId.xy] = float4(pow(saturate(color), 1.0 / 2.2), 1.0);
}
