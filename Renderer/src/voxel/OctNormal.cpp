#include "thesis/voxel/OctNormal.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace thesis::voxel {
namespace {

float signNotZero(float value)
{
    return value >= 0.0f ? 1.0f : -1.0f;
}

std::uint16_t toByte(float value) // [-1, 1] -> [0, 255]
{
    return static_cast<std::uint16_t>(std::lround(std::clamp(value * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f));
}

float fromByte(std::uint16_t value) // [0, 255] -> [-1, 1]
{
    return static_cast<float>(value) / 255.0f * 2.0f - 1.0f;
}

} // namespace

std::uint16_t encodeNormal(const glm::vec3& normal)
{
    const float sum = std::abs(normal.x) + std::abs(normal.y) + std::abs(normal.z);
    if (sum == 0.0f) {
        return encodeNormal({0.0f, 0.0f, 1.0f});
    }
    float x = normal.x / sum;
    float y = normal.y / sum;
    if (normal.z < 0.0f) { // fold the lower half of the octahedron outwards
        const float foldedX = (1.0f - std::abs(y)) * signNotZero(x);
        const float foldedY = (1.0f - std::abs(x)) * signNotZero(y);
        x = foldedX;
        y = foldedY;
    }
    return static_cast<std::uint16_t>(toByte(x) | (toByte(y) << 8));
}

glm::vec3 decodeNormal(std::uint16_t packed)
{
    float x = fromByte(packed & 0xFFu);
    float y = fromByte(static_cast<std::uint16_t>(packed >> 8));
    const float z = 1.0f - std::abs(x) - std::abs(y);
    const float fold = std::max(-z, 0.0f);
    x += x >= 0.0f ? -fold : fold;
    y += y >= 0.0f ? -fold : fold;
    return glm::normalize(glm::vec3(x, y, z));
}

} // namespace thesis::voxel
