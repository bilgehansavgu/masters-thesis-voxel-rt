#include "thesis/core/RayBox.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace thesis::core {

std::optional<glm::vec2> rayBoxInterval(glm::vec3 origin, glm::vec3 direction, const Aabb& box)
{
    float t0 = 0.0f, t1 = std::numeric_limits<float>::infinity();
    for (int axis = 0; axis < 3; ++axis) {
        const float inverse = 1.0f / direction[axis];
        float a = (box.lo[axis] - origin[axis]) * inverse;
        float b = (box.hi[axis] - origin[axis]) * inverse;
        if (a > b)
            std::swap(a, b);
        t0 = std::max(t0, a);
        t1 = std::min(t1, b);
    }
    if (t0 > t1)
        return std::nullopt;
    return glm::vec2{t0, t1};
}

} // namespace thesis::core
