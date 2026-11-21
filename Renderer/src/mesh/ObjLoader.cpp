#include "thesis/mesh/TriangleMesh.h"

#include <tiny_obj_loader.h>

#include <stdexcept>

namespace thesis::mesh {

TriangleMesh loadObj(const std::filesystem::path& path)
{
    const std::string file = path.string();
    tinyobj::ObjReaderConfig config;
    config.triangulate = true;
    config.vertex_color = false;
    config.mtl_search_path = path.parent_path().string(); // materials are not used, but a missing one is no error
    tinyobj::ObjReader reader;
    if (!reader.ParseFromFile(file, config)) {
        throw std::runtime_error("Cannot read OBJ file '" + file + "': " + reader.Error());
    }

    const tinyobj::attrib_t& attributes = reader.GetAttrib();
    TriangleMesh mesh;
    mesh.name = path.stem().string();
    mesh.positions.resize(attributes.vertices.size() / 3);
    for (std::size_t i = 0; i < mesh.positions.size(); ++i) {
        mesh.positions[i] = {attributes.vertices[3 * i], attributes.vertices[3 * i + 1],
                             attributes.vertices[3 * i + 2]};
    }
    for (const tinyobj::shape_t& shape : reader.GetShapes()) {
        // With triangulate set, every face has three corners.
        for (const tinyobj::index_t& corner : shape.mesh.indices) {
            if (corner.vertex_index < 0 || static_cast<std::size_t>(corner.vertex_index) >= mesh.positions.size()) {
                throw std::runtime_error("OBJ file '" + file + "': a face names a vertex that does not exist");
            }
            mesh.indices.push_back(static_cast<std::uint32_t>(corner.vertex_index));
        }
    }
    if (mesh.indices.empty()) {
        throw std::runtime_error("OBJ file '" + file + "' holds no triangles");
    }
    return mesh;
}

} // namespace thesis::mesh
