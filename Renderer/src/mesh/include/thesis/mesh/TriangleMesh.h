#pragma once

// ============================================================================
// READING ORDER: FILE 1A (Step 1 Offline Pipeline: Triangle Mesh in CPU memory)
// ============================================================================
// What is a Triangle Mesh?
// Standard 3D models (like the Stanford Lucy or Dragon scans) are stored as a "hollow shell"
// made of millions of flat triangles stitched together at their corners (vertices).
// Instead of storing three 3D points per triangle (which duplicates every shared corner 6 times),
// a mesh stores:
//   1. `positions`: a list of unique 3D vertex coordinates `(x, y, z)`.
//   2. `indices`: a flat list of integer vertex numbers. Every 3 consecutive numbers
//      `(indices[3*i], indices[3*i + 1], indices[3*i + 2])` pick the 3 corners of triangle `i`.

#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace thesis::mesh {

// A triangle mesh: the input of the voxelizer. Three entries of `indices` form one triangle;
// each entry is an index into `positions`.
struct TriangleMesh {
    std::string name;
    std::vector<glm::vec3> positions;   // unique 3D vertex positions
    std::vector<std::uint32_t> indices; // 3 indices per triangle (size is always a multiple of 3)

    [[nodiscard]] std::size_t triangleCount() const { return indices.size() / 3; }
};

// Reads a PLY file (ASCII or binary, little or big endian; the Stanford scans are big endian).
// Only positions and faces are read. Faces with more than three corners (quads/polygons) are
// split into triangles automatically. Throws std::runtime_error if the file cannot be read or
// holds no triangles.
[[nodiscard]] TriangleMesh loadPly(const std::filesystem::path& path);

// Generates a closed 3D sphere made of triangles (UV sphere with north/south poles and latitude
// rings), used for unit tests (`TestVoxel.cpp`, `TestEncodings.cpp`) without needing external files.
// `segments` is the number of triangles around the equator (at least 3).
[[nodiscard]] TriangleMesh makeSphere(const glm::vec3& center, float radius, std::uint32_t segments);

} // namespace thesis::mesh
