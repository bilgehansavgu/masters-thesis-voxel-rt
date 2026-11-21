#pragma once

// Side: CPU

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
    std::vector<glm::vec3> positions;
    std::vector<std::uint32_t> indices;

    [[nodiscard]] std::size_t triangleCount() const { return indices.size() / 3; }
};

// Reads a PLY file (ASCII or binary, little or big endian; the Stanford scans are big endian).
// Only positions and faces are read. Faces with more than three corners are split into
// triangles. Throws std::runtime_error if the file cannot be read or holds no triangles.
[[nodiscard]] TriangleMesh loadPly(const std::filesystem::path& path);

// A sphere made of triangles, for tests and for a model that needs no file.
// `segments` is the number of triangles around the equator (at least 3).
[[nodiscard]] TriangleMesh makeSphere(const glm::vec3& center, float radius, std::uint32_t segments);

} // namespace thesis::mesh
