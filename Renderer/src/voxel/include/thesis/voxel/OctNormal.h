#pragma once

// ============================================================================
// READING ORDER: FILE 2A (Compressing a 3D surface normal into 16 bits)
// ============================================================================
// GRAPHICS BASICS: Why and how do we pack a 3D normal into 16 bits?
// Every surface voxel needs a 3D unit normal vector `(nx, ny, nz)` (length = 1) so the shader
// knows which direction the surface faces for lighting.
// A naive `glm::vec3` takes 3 floats = 12 bytes (96 bits) per voxel — way too large when a model
// has 56 million voxels!
//
// Because a unit vector always has length 1, it only has 2 degrees of freedom (latitude &
// longitude on a unit sphere). **Octahedral normal encoding** projects the unit sphere onto an
// 8-sided diamond (an octahedron, defined by |x| + |y| + |z| = 1) and unfolds that octahedron
// flat onto a 2D square `[-1, 1] x [-1, 1]`:
//   - The upper hemisphere (`z >= 0`) maps to the inner diamond `|x| + |y| <= 1`.
//   - The lower hemisphere (`z < 0`) is folded outward into the 4 corners of the `[-1, 1]` square.
// Finally, each of the 2 square coordinates is quantized to an 8-bit byte `[0..255]`, giving one
// compact `std::uint16_t` (2 bytes = 16 bits) per voxel with at most ~1 degree of angular error!

#include <glm/vec3.hpp>

#include <cstdint>

namespace thesis::voxel {

// A unit vector packed into 16 bits with the octahedral mapping: the sphere is projected onto
// an octahedron, the octahedron is unfolded into a square, and each of the two square
// coordinates is stored in 8 bits. The largest error is about one degree.
[[nodiscard]] std::uint16_t encodeNormal(const glm::vec3& normal);
[[nodiscard]] glm::vec3 decodeNormal(std::uint16_t packed);

} // namespace thesis::voxel
