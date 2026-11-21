#include "thesis/mesh/TriangleMesh.h"

#include <bit>
#include <cstdint>
#include <fstream>
#include <stdexcept>

namespace thesis::mesh {

// The values are written as they are in memory, which is little endian on every machine this
// program runs on.
static_assert(std::endian::native == std::endian::little);

void savePly(const std::filesystem::path& path, const TriangleMesh& mesh)
{
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot write PLY file '" + path.string() + "'");
    }
    file << "ply\nformat binary_little_endian 1.0\n"
         << "element vertex " << mesh.positions.size() << "\nproperty float x\nproperty float y\nproperty float z\n"
         << "element face " << mesh.triangleCount() << "\nproperty list uchar uint vertex_indices\nend_header\n";
    file.write(reinterpret_cast<const char*>(mesh.positions.data()),
               static_cast<std::streamsize>(mesh.positions.size() * sizeof(glm::vec3)));
    for (std::size_t triangle = 0; triangle < mesh.triangleCount(); ++triangle) {
        const std::uint8_t corners = 3;
        file.write(reinterpret_cast<const char*>(&corners), 1);
        file.write(reinterpret_cast<const char*>(&mesh.indices[3 * triangle]), 3 * sizeof(std::uint32_t));
    }
    if (!file) {
        throw std::runtime_error("Cannot write PLY file '" + path.string() + "'");
    }
}

} // namespace thesis::mesh
