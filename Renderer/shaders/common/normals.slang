// A unit normal packed into 16 bits (octahedral encoding, 8 bits for each of two coordinates).
// The same steps as decodeNormal() in src/voxel/OctNormal.cpp. tests/TestNormals.cpp runs this
// code for all 65,536 values and compares the result with the C++ code.

public float3 decodeNormal(uint packed)
{
    float x = float(packed & 0xFF) / 255.0 * 2.0 - 1.0;
    float y = float((packed >> 8) & 0xFF) / 255.0 * 2.0 - 1.0;
    const float z = 1.0 - abs(x) - abs(y);
    const float fold = max(-z, 0.0);
    x += x >= 0.0 ? -fold : fold;
    y += y >= 0.0 ? -fold : fold;
    return normalize(float3(x, y, z));
}
