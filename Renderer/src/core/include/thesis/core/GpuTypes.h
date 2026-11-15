#pragma once

// ============================================================================
// READING ORDER: FILE 1B (the C++ mirror of shaders/common/types.slang)
// ============================================================================
// C++ REFRESHER: `#pragma once` tells the compiler to include this header file
// only once per `.cpp` file, preventing "redefinition" errors.

#include <glm/vec3.hpp> // GLM (OpenGL Mathematics): standard C++ math library for 3D vectors (glm::vec3)

#include <cstddef> // for offsetof()
#include <cstdint> // for fixed-width integer types like std::uint32_t (always 32 bits / 4 bytes)

namespace thesis::core {

// CPU mirrors of the structs in shaders/common/types.slang.
//
// WHY DO WE NEED THIS IN BOTH C++ AND SLANG?
// The CPU prepares the scene (and tests read back the results), while the GPU runs the shaders.
// Both sides read and write the exact same raw bytes in GPU memory buffers.
// If C++ thinks `direction` starts at byte 16, the shader MUST also read `direction` at byte 16.
//
// The GPU reads storage buffers using the `std430` memory layout rule.
// The `static_assert` lines at the bottom check the C++ byte sizes and offsets at compile time,
// and `tests/TestShaderLayouts.cpp` checks the compiled shader JSON reports against this file.

// One ray of the ray list that every tracer consumes.
// Parametric ray equation: P(t) = origin + t * direction, for t in [tMin, tMax].
struct Ray {
    glm::vec3 origin;    // 3 floats (x, y, z) = 12 bytes: where the ray starts in 3D world space
    float tMin;          // 1 float  = 4 bytes: closest valid hit distance (usually 0.0)
    glm::vec3 direction; // 3 floats (x, y, z) = 12 bytes: unit vector pointing where the ray goes
    float tMax;          // 1 float  = 4 bytes: farthest valid hit distance (e.g., 1e30 for infinity)
};

// One entry of the hit list that every tracer produces (at the exact same array index as its Ray).
struct Hit {
    float t;                 // distance along the ray; equals the ray's tMax on a miss
    std::uint32_t primitive; // index into the scene's primitives array, or kNoHit if missed
    std::uint32_t face;      // 0..5 = -X, +X, -Y, +Y, -Z, +Z: the box side the ray entered through
    std::uint32_t reserved;  // padding to make `Hit` 16 bytes (4 * 4 bytes)
};

// C++ REFRESHER: `inline constexpr` defines a compile-time constant in a header file without
// causing "multiple definition" linker errors when included by multiple .cpp files.
// `0xFFFFFFFFu` is the maximum unsigned 32-bit integer, used as a special "no hit" marker.
inline constexpr std::uint32_t kNoHit = 0xFFFFFFFFu;

// Axis-Aligned Bounding Box (AABB): a non-rotated 3D box from corner `lo` (minX, minY, minZ)
// to corner `hi` (maxX, maxY, maxZ).
//
// In C++, `glm::vec3` is 12 bytes with 4-byte alignment (unlike Slang's 16-byte aligned `float3`),
// so `lo` (bytes 0..11) and `hi` (bytes 12..23) pack directly back-to-back as six floats in a row.
// That matches Vulkan's hardware BVH input struct `VkAabbPositionsKHR`, with 8 bytes of padding
// (`pad0`, `pad1`) at the end to reach 32 bytes total.
struct Aabb {
    glm::vec3 lo;
    glm::vec3 hi;
    float pad0 = 0.0f;
    float pad1 = 0.0f;
};

// C++ REFRESHER: `static_assert` runs when you compile the code. If the condition is false,
// compilation stops immediately with an error. `offsetof(Struct, member)` returns the byte
// offset of a field from the start of the struct.
static_assert(sizeof(Ray) == 32);
static_assert(offsetof(Ray, tMin) == 12 && offsetof(Ray, direction) == 16 && offsetof(Ray, tMax) == 28);
static_assert(sizeof(Hit) == 16);
static_assert(sizeof(Aabb) == 32);
static_assert(offsetof(Aabb, hi) == 12 && offsetof(Aabb, pad0) == 24);

} // namespace thesis::core
