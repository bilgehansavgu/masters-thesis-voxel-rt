// Shared GPU data formats. CPU mirror: src/core/include/thesis/core/GpuTypes.h.
// tests/TestShaderLayouts.cpp compares the two field by field, using the compiler's own layout
// report, so a mismatch fails the tests instead of producing garbage.
//
// Binding slots are not written here: they come from shared/include/thesis/shared/Bindings.h,
// which C++ and shaders both include.

public struct Ray
{
    public float3 origin;
    public float  tMin;
    public float3 direction;
    public float  tMax;
};

public struct Hit
{
    public float t;          // distance along the ray; equals the ray's tMax on a miss
    public uint  primitive;  // index of the primitive hit, or kNoHit
    public uint  face;       // 0..5 = -X, +X, -Y, +Y, -Z, +Z: the side the ray entered through
    public uint  reserved;
};

// Six floats in a row, then padding: this is also the layout the GPU's search-structure builder
// reads (VkAabbPositionsKHR, stride 32). Scalar fields because a float3 member would be
// aligned to 16 bytes and break that layout.
public struct Aabb
{
    public float minX, minY, minZ;
    public float maxX, maxY, maxZ;
    public float pad0, pad1;

    public float3 lo() { return float3(minX, minY, minZ); }
    public float3 hi() { return float3(maxX, maxY, maxZ); }
};

public static const uint kNoHit = 0xFFFFFFFF;

public Hit makeMiss(Ray ray)
{
    Hit hit;
    hit.t = ray.tMax;
    hit.primitive = kNoHit;
    hit.face = 0;
    hit.reserved = 0;
    return hit;
}
