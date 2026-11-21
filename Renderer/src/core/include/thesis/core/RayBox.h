#pragma once

// Side: CPU

#include "thesis/core/GpuTypes.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <optional>

namespace thesis::core {

// The distances [t0, t1] (t1 >= t0 >= 0) at which the ray origin + t * direction is inside `box`,
// or none (slab test). For drawing and explaining rays on the CPU; the GPU has its own test in
// shaders/common/aabb.slang.
[[nodiscard]] std::optional<glm::vec2> rayBoxInterval(glm::vec3 origin, glm::vec3 direction, const Aabb& box);

} // namespace thesis::core
