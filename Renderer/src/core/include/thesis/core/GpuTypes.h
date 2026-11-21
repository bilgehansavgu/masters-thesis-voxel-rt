#pragma once

// Side: CPU

#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>

namespace thesis::core {

// CPU mirrors of the structs in shaders/common/types.slang.
// The GPU reads them with std430 layout. The static_asserts below pin the C++ side down, and
// tests/TestShaderLayouts.cpp checks the shader side against it field by field.

// One ray of the ray list that every tracer consumes.
struct Ray {
    glm::vec3 origin;
    float tMin;
    glm::vec3 direction;
    float tMax;
};

// One entry of the hit list that every tracer produces (same index as its ray).
struct Hit {
    float t;                  // distance along the ray; equals the ray's tMax on a miss
    std::uint32_t primitive;  // index into the scene's primitives, or kNoHit
    std::uint32_t face;       // 0..5 = -X, +X, -Y, +Y, -Z, +Z: the side the ray entered through
    std::uint32_t attributes; // what the primitive kind stores for the hit point, see thesis/shared/Hit.h
};

// The work one ray caused. Only the counting variant of a tracer writes it (ITracer::traceCounting).
struct RayWork {
    std::uint32_t primitiveTests; // primitives the ray was tested against (for a voxel model: bricks)
    std::uint32_t cells;          // cells of primary grids the ray visited
    std::uint32_t runs;           // runs the ray was tested against
    std::uint32_t hitCell;        // kind of the cell with the hit: THESIS_HIT_CELL_* in thesis/shared/Hit.h
    // Cells or nodes of the tracer's own search structure that the ray read. 0 for the hardware
    // tracer: the GPU does not tell how many nodes of its BVH a ray visits.
    std::uint32_t structureSteps;
    // Cells of the min/max mipmaps of the bricks that the ray read. 0 for an encoding without mipmap.
    std::uint32_t mipmapCells;
    std::uint32_t mipmapSkips;   // of those, the cells whose square the ray passed by, so the walk skipped it
    std::uint32_t skippedLevels; // bit L is set if the walk skipped a square of level L (2^L x 2^L cells)
};

// One step of the ray log, or its header (entry 0). The meaning of the fields depends on `kind`:
// thesis/shared/RayLog.h.
struct RayLogEntry {
    std::uint32_t kind;
    std::uint32_t a;
    std::uint32_t b;
    std::uint32_t c;
    std::uint32_t d;
    std::uint32_t e;
    std::uint32_t f;
    float t;
};

inline constexpr std::uint32_t kNoHit = 0xFFFFFFFFu;

// Axis-aligned bounding box. Also the direct input for the GPU's own search structure, which
// reads six floats in a row (VkAabbPositionsKHR: min x, y, z, max x, y, z) every 32 bytes.
// That is why lo and hi are packed back to back with the padding at the end.
struct Aabb {
    glm::vec3 lo;
    glm::vec3 hi;
    float pad0 = 0.0f;
    float pad1 = 0.0f;
};

static_assert(sizeof(Ray) == 32);
static_assert(offsetof(Ray, tMin) == 12 && offsetof(Ray, direction) == 16 && offsetof(Ray, tMax) == 28);
static_assert(sizeof(Hit) == 16);
static_assert(sizeof(RayWork) == 32);
static_assert(sizeof(RayLogEntry) == 32);
static_assert(sizeof(Aabb) == 32);
static_assert(offsetof(Aabb, hi) == 12 && offsetof(Aabb, pad0) == 24);

} // namespace thesis::core
