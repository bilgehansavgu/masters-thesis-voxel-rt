#pragma once

#include "thesis/core/GpuTypes.h"

#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
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

// A scene as the tracers see it: a list of primitives, each with a bounding box.
//
// `primitiveKind` names the shader module that knows what is inside a primitive's box
// (shaders/primitives/<kind>/). In step 0 the kind is "box": the primitive is its box and
// needs no extra data. Later the kind is an encoded voxel brick: the bounds are the bricks'
// bounds and `primitiveData` holds the encoded bricks.
struct Scene {
    std::string name;
    std::string primitiveKind;
    std::vector<Aabb> primitiveBounds;
    std::vector<PrimitiveData> primitiveData;
    Bounds bounds;
};

} // namespace thesis::core
