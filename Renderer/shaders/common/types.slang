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
    public uint  attributes; // what the primitive kind stores for the hit point, see thesis/shared/Hit.h
};

// The work one ray caused. Only the counting variant of a tracer writes it (work.slang).
public struct RayWork
{
    public uint primitiveTests; // primitives the ray was tested against (for a voxel model: bricks)
    public uint cells;          // cells of primary grids the ray visited
    public uint runs;           // runs the ray was tested against
    public uint hitCell;        // kind of the cell with the hit: THESIS_HIT_CELL_* in thesis/shared/Hit.h
    public uint structureSteps; // cells or nodes of the tracer's own search structure the ray read; 0 if not known
    public uint mipmapCells;    // cells of the min/max mipmaps of the bricks the ray read; 0 without mipmap
    public uint mipmapSkips;    // of those, the cells whose square the ray passed by, so the walk skipped it
    public uint skippedLevels;  // bit L is set if the walk skipped a square of level L (2^L x 2^L cells)
    // GPU clock cycles from the start of the ray to its last stored count, read with the clock of
    // the warp (VK_KHR_shader_clock, subgroup scope). A ray that finishes before others of its
    // warp waits for them, so this is the cost the GPU pays for the ray. Counting variant only:
    // it times the counting shader, so it is for looking, never a result.
    public uint cycles;
    public uint clockStart; // the clock when the ray started (low 32 bits); resume() needs it
};

// One step of the ray log, or its header (entry 0). The meaning of the fields depends on `kind`:
// thesis/shared/RayLog.h.
public struct RayLogEntry
{
    public uint  kind;
    public uint  a;
    public uint  b;
    public uint  c;
    public uint  d;
    public uint  e;
    public uint  f;
    public float t;
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

// What a primitive kind reports when a ray hits one of its primitives.
public struct PrimitiveHit
{
    public float t;      // distance along the ray
    public uint  face;   // as in Hit
    public uint  detail; // for the kind's own use: where inside the primitive the hit is
};

public static const uint kNoHit = 0xFFFFFFFF;

// Larger than any distance along a ray: "the ray never gets there".
public static const float kNever = 3.0e38;

public Hit makeMiss(Ray ray)
{
    Hit hit;
    hit.t = ray.tMax;
    hit.primitive = kNoHit;
    hit.face = 0;
    hit.attributes = 0;
    return hit;
}
