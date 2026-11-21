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
// Reads a Wavefront OBJ file. Only positions and faces are read; every object and group of the
// file goes into one mesh. Faces with more than three corners are split into triangles. Throws
// std::runtime_error if the file cannot be read or holds no triangles.
[[nodiscard]] TriangleMesh loadObj(const std::filesystem::path& path);
// Reads a glTF 2.0 file (.gltf with its buffers, or binary .glb). Every triangle primitive of the
// default scene goes into one mesh, placed by the transforms of its nodes. Points and lines are
// not surfaces and are left out. Throws std::runtime_error if the file cannot be read, needs a
// compression extension, or holds no triangles.
[[nodiscard]] TriangleMesh loadGltf(const std::filesystem::path& path);

// The mesh files loadMesh() reads, as lower-case extensions with the dot (".ply", ...).
[[nodiscard]] const std::vector<std::string>& meshFileExtensions();
// True if loadMesh() reads files of this extension (any case).
[[nodiscard]] bool isMeshFile(const std::filesystem::path& path);
// Reads a mesh file of any kind in meshFileExtensions(), chosen by the extension. Throws
// std::runtime_error for another extension and for a file the reader of its kind refuses.
[[nodiscard]] TriangleMesh loadMesh(const std::filesystem::path& path);

// Writes a binary little-endian PLY file that loadPly() reads back unchanged. Throws
// std::runtime_error if the file cannot be written.
void savePly(const std::filesystem::path& path, const TriangleMesh& mesh);

// A sphere made of triangles, for tests and for a model that needs no file.
// `segments` is the number of triangles around the equator (at least 3).
[[nodiscard]] TriangleMesh makeSphere(const glm::vec3& center, float radius, std::uint32_t segments);

} // namespace thesis::mesh
