#pragma once

// Side: CPU

#include "thesis/core/GpuTypes.h"

#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace thesis::core {

struct Bounds {
    glm::vec3 lo{std::numeric_limits<float>::max()};
    glm::vec3 hi{std::numeric_limits<float>::lowest()};

    void extend(const glm::vec3& point);
    void extend(const Aabb& box);
    [[nodiscard]] bool empty() const;
    [[nodiscard]] glm::vec3 center() const;
    // Radius of the bounding sphere around center().
    [[nodiscard]] float radius() const;
};

// Extra data a primitive kind's shader reads besides the bounds, e.g. encoded voxel bricks.
// `binding` is the shader binding the kind's primitive.slang declares for it.
struct PrimitiveData {
    std::uint32_t binding = 0;
    std::string name;
    std::vector<std::byte> bytes;
};

// A regular grid of equal cubes: cell (x, y, z) is the cube from origin + cellSize * (x, y, z)
// to origin + cellSize * (x + 1, y + 1, z + 1).
struct RegularGrid {
    glm::vec3 origin{0.0f};
    float cellSize = 1.0f;
    glm::uvec3 size{0}; // cells along each axis
};

// A scene as the tracers see it: a list of primitives, each with a bounding box.
//
// `primitiveKind` names the shader module that knows what is inside a primitive's box
// (shaders/primitives/<kind>/). For the kind "box" the primitive is its box and needs no extra
// data. For an encoded voxel model the kind is the encoding: the bounds are the boxes of the
// bricks and `primitiveData` holds the encoded bricks.
struct Scene {
    std::string name;
    std::string primitiveKind;
    std::vector<Aabb> primitiveBounds;
    std::vector<PrimitiveData> primitiveData;
    Bounds bounds;
    // A hint for tracers that build their own search structure. Set when every primitive lies
    // inside one cell of this grid, as the bricks of a voxel model do. No tracer has to use it.
    std::optional<RegularGrid> primitiveGrid;
};

} // namespace thesis::core
