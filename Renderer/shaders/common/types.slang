// ============================================================================
// READING ORDER: FILE 11A (Part 2: GPU Rendering & Tracing Pipeline)
// ============================================================================
// Shared GPU data formats written in Slang (the shader language that runs on the GPU).
// CPU mirror: src/core/include/thesis/core/GpuTypes.h.
// tests/TestShaderLayouts.cpp compares the two field by field, using the compiler's own layout
// report, so a mismatch fails the tests instead of producing garbage.
//
// Binding slots are not written here: they come from shared/include/thesis/shared/Bindings.h,
// which C++ and shaders both include.

// RAY TRACING BASICS: What is a Ray?
// A ray is a half-line in 3D space defined by the parametric equation:
//     P(t) = origin + t * direction
// where:
//   - `origin` is the 3D starting point (x, y, z) of the ray (e.g., the camera's eye position).
//   - `direction` is a 3D unit vector (length = 1) pointing where the ray travels.
//   - `t` is the distance along the ray from `origin`.
//   - `[tMin, tMax]` is the valid distance interval. Any intersection closer than `tMin`
//     (behind or too close to the start) or farther than `tMax` is ignored.
//
// MEMORY LAYOUT NOTE:
// On the GPU, a `float3` followed by a `float` packs neatly into 16 bytes (4 floats * 4 bytes).
// So `Ray` is 32 bytes total (two 16-byte chunks), which matches a single GPU memory sector.
public struct Ray
{
    public float3 origin;     // 12 bytes (offset 0)
    public float  tMin;       //  4 bytes (offset 12) - minimum valid hit distance
    public float3 direction;  // 12 bytes (offset 16) - normalized direction vector
    public float  tMax;       //  4 bytes (offset 28) - maximum valid hit distance
};

// RAY TRACING BASICS: What is a Hit?
// For every Ray in the input buffer, a tracer writes one `Hit` at the exact same array index.
// It records the closest surface that the ray intersected:
//   - `t`: how far along the ray the hit occurred. Plug `t` into `origin + t * direction`
//     to get the exact 3D hit point. On a miss, `t` stays equal to `ray.tMax`.
//   - `primitive`: which object (0, 1, 2, ...) was hit, or `kNoHit` (0xFFFFFFFF) if the ray
//     missed everything and flew into the sky.
//   - `face`: which of the 6 sides of the box/voxel the ray entered through:
//     0 = -X (left), 1 = +X (right), 2 = -Y (bottom), 3 = +Y (top), 4 = -Z (back), 5 = +Z (front).
//   - `reserved`: extra 4-byte slot so the struct size is 16 bytes (a multiple of 16 bytes
//     aligns cleanly in GPU memory).
public struct Hit
{
    public float t;          // distance along the ray; equals the ray's tMax on a miss
    public uint  primitive;  // index of the primitive hit, or kNoHit
    public uint  face;       // 0..5 = -X, +X, -Y, +Y, -Z, +Z: the side the ray entered through
    public uint  reserved;   // padding / reserved for future material or voxel data
};

// RAY TRACING BASICS: What is an AABB (Axis-Aligned Bounding Box)?
// A 3D box whose edges are parallel to the X, Y, and Z coordinate axes.
// Because it is not rotated, we only need two opposite corners to define it completely:
//   - `lo`: the minimum corner (minX, minY, minZ)
//   - `hi`: the maximum corner (maxX, maxY, maxZ)
// A point (x, y, z) is inside the box if and only if:
//   minX <= x <= maxX,  minY <= y <= maxY,  and  minZ <= z <= maxZ.
//
// Six floats in a row, then padding: this is also the exact byte layout the GPU's hardware
// BVH builder reads (VkAabbPositionsKHR, stride 32). We declare six scalar `float` fields
// instead of `float3` because in Slang/SPIR-V a `float3` member inside a struct is aligned to
// 16 bytes, which would insert unwanted gap bytes between `minZ` and `maxX`.
public struct Aabb
{
    public float minX, minY, minZ;
    public float maxX, maxY, maxZ;
    public float pad0, pad1;

    // Helper methods to reconstruct 3D vectors when doing math in shaders.
    public float3 lo() { return float3(minX, minY, minZ); }
    public float3 hi() { return float3(maxX, maxY, maxZ); }
};

// Sentinel value (all 32 bits set to 1, i.e., 4,294,967,295) meaning "the ray hit nothing".
public static const uint kNoHit = 0xFFFFFFFF;

// Creates a default Hit representing a ray that missed all objects in the scene.
// Setting `hit.t = ray.tMax` lets tracers use `candidate.t < best.t` to both check `ray.tMax`
// and keep only the closest hit in a single comparison.
public Hit makeMiss(Ray ray)
{
    Hit hit;
    hit.t = ray.tMax;
    hit.primitive = kNoHit;
    hit.face = 0;
    hit.reserved = 0;
    return hit;
}
