#include "thesis/core/Scene.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace thesis::core {

void Bounds::extend(const glm::vec3& point)
{
    lo = glm::min(lo, point);
    hi = glm::max(hi, point);
}

void Bounds::extend(const Aabb& box)
{
    extend(box.lo);
    extend(box.hi);
}

bool Bounds::empty() const
{
    return lo.x > hi.x || lo.y > hi.y || lo.z > hi.z;
}

glm::vec3 Bounds::center() const
{
    return empty() ? glm::vec3(0.0f) : 0.5f * (lo + hi);
}

float Bounds::radius() const
{
    return empty() ? 0.0f : 0.5f * glm::length(hi - lo);
}

} // namespace thesis::core
