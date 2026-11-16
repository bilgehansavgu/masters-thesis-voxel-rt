#pragma once

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
static_assert(sizeof(RayWork) == 16);
static_assert(sizeof(Aabb) == 32);
static_assert(offsetof(Aabb, hi) == 12 && offsetof(Aabb, pad0) == 24);

} // namespace thesis::core
