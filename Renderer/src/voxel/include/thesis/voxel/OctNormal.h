#pragma once

// Side: CPU

#include <glm/vec3.hpp>

#include <cstdint>

namespace thesis::voxel {

// A unit vector packed into 16 bits with the octahedral mapping: the sphere is projected onto
// an octahedron, the octahedron is unfolded into a square, and each of the two square
// coordinates is stored in 8 bits. The largest error is about one degree.
[[nodiscard]] std::uint16_t encodeNormal(const glm::vec3& normal);
[[nodiscard]] glm::vec3 decodeNormal(std::uint16_t packed);

} // namespace thesis::voxel
