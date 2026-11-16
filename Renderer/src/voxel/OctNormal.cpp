#include "thesis/voxel/OctNormal.h"

// ============================================================================
// READING ORDER: FILE 2B (Octahedral 16-bit normal packing implementation)
// ============================================================================

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace thesis::voxel {
namespace {

float signNotZero(float value)
{
    return value >= 0.0f ? 1.0f : -1.0f;
}

// Quantizes a float in [-1, 1] into an 8-bit integer in [0, 255].
std::uint16_t toByte(float value) // [-1, 1] -> [0, 255]
{
    return static_cast<std::uint16_t>(std::lround(std::clamp(value * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f));
}

// Unpacks an 8-bit integer in [0, 255] back into a float in [-1, 1].
float fromByte(std::uint16_t value) // [0, 255] -> [-1, 1]
{
    return static_cast<float>(value) / 255.0f * 2.0f - 1.0f;
}

} // namespace

std::uint16_t encodeNormal(const glm::vec3& normal)
{
    // Step 1: Project the 3D vector onto the unit octahedron (|x| + |y| + |z| = 1)
    // by dividing by the L1 norm (`|x| + |y| + |z|`).
    const float sum = std::abs(normal.x) + std::abs(normal.y) + std::abs(normal.z);
    if (sum == 0.0f) {
        return encodeNormal({0.0f, 0.0f, 1.0f});
    }
    float x = normal.x / sum;
    float y = normal.y / sum;

    // Step 2: If the normal points downward (`z < 0`, lower half of the octahedron),
    // reflect `(x, y)` across the diagonal edges `|x| + |y| = 1` into the outer corners of the
    // `[-1, 1] x [-1, 1]` square.
    if (normal.z < 0.0f) { // fold the lower half of the octahedron outwards
        const float foldedX = (1.0f - std::abs(y)) * signNotZero(x);
        const float foldedY = (1.0f - std::abs(x)) * signNotZero(y);
        x = foldedX;
        y = foldedY;
    }

    // Step 3: Pack `x` into the low 8 bits (bits 0..7) and `y` into the high 8 bits (bits 8..15).
    return static_cast<std::uint16_t>(toByte(x) | (toByte(y) << 8));
}

glm::vec3 decodeNormal(std::uint16_t packed)
{
    // Step 1: Extract the low byte (`packed & 0xFF`) and high byte (`packed >> 8`) back to [-1, 1].
    float x = fromByte(packed & 0xFFu);
    float y = fromByte(static_cast<std::uint16_t>(packed >> 8));

    // Step 2: Reconstruct `z` on the octahedron (`|x| + |y| + z = 1`).
    // If `z < 0`, the point was in one of the outer corners, so unfold `(x, y)` back inward.
    const float z = 1.0f - std::abs(x) - std::abs(y);
    const float fold = std::max(-z, 0.0f);
    x += x >= 0.0f ? -fold : fold;
    y += y >= 0.0f ? -fold : fold;

    // Step 3: Normalize back from the octahedron onto the unit sphere (length = 1).
    return glm::normalize(glm::vec3(x, y, z));
}

} // namespace thesis::voxel
