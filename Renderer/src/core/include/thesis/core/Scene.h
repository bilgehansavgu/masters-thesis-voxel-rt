#pragma once

// ============================================================================
// READING ORDER: FILE 9 (How a 3D scene is represented on the CPU)
// ============================================================================

#include "thesis/core/GpuTypes.h"

#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace thesis::core {

// Tracks the overall Axis-Aligned Bounding Box around the entire scene.
//
// C++ REFRESHER:
// - `lo` is initialized to `+max()` (huge positive number) and `hi` to `lowest()` (huge negative
//   number). That way, `lo > hi` initially (an "empty" box), and the very first `extend(point)`
//   call overwrites both `lo` and `hi` with that first point's coordinates.
// - `[[nodiscard]]` is a C++17 attribute that warns you at compile time if you call a getter
//   function (like `scene.bounds.empty()`) and accidentally ignore its return value.
struct Bounds {
    glm::vec3 lo{std::numeric_limits<float>::max()};
    glm::vec3 hi{std::numeric_limits<float>::lowest()};

    // Grows the bounding box so that it includes `point` (or another `box`).
    void extend(const glm::vec3& point);
    void extend(const Aabb& box);

    [[nodiscard]] bool empty() const;
    [[nodiscard]] glm::vec3 center() const;
    // Radius of the bounding sphere around center() (half the main diagonal of the box).
    [[nodiscard]] float radius() const;
};

// Extra data a primitive kind's shader reads besides the bounds, e.g. encoded voxel bricks.
// `binding` is the shader binding slot (9..15) that the kind's `primitive.slang` declares for it.
// `std::vector<std::byte>` is a dynamic array of raw untyped bytes on the CPU, ready to be
// uploaded verbatim into a GPU storage buffer.
struct PrimitiveData {
    std::uint32_t binding = 0;
    std::string name;
    std::vector<std::byte> bytes;
};

// A scene as the tracers see it: a list of primitives, each with a bounding box.
//
// `primitiveKind` names the shader module that knows what is inside a primitive's box
// (shaders/primitives/<kind>/). In step 0 the kind is "box": the primitive is its box and
// needs no extra data. Later the kind is an encoded voxel brick: `primitiveBounds` holds the
// 3D bounding box of each brick, and `primitiveData` holds the compressed voxel buffers.
struct Scene {
    std::string name;
    std::string primitiveKind;
    std::vector<Aabb> primitiveBounds;        // uploaded to binding slot 8 (and used to build the BVH)
    std::vector<PrimitiveData> primitiveData; // uploaded to binding slots 9..15
    Bounds bounds;                            // overall bounding box of the whole scene (used to aim the camera)
};

} // namespace thesis::core
