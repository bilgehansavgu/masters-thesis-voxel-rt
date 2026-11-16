// Test shader: decodes every 16-bit value with the decodeNormal() that the shading uses.
// tests/TestNormals.cpp compares the result with the C++ decodeNormal().

import normals;

[[vk::binding(0, 0)]] RWStructuredBuffer<float4> gNormals; // 65,536 entries

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 threadId : SV_DispatchThreadID)
{
    if (threadId.x < 65536)
        gNormals[threadId.x] = float4(decodeNormal(threadId.x), 0.0);
}
