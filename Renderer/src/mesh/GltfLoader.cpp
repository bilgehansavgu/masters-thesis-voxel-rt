#include "thesis/mesh/TriangleMesh.h"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

namespace thesis::mesh {
namespace {

struct FreeData {
    void operator()(cgltf_data* data) const { cgltf_free(data); }
};

const char* resultName(cgltf_result result)
{
    switch (result) {
    case cgltf_result_data_too_short:
        return "the file is too short";
    case cgltf_result_unknown_format:
        return "unknown format";
    case cgltf_result_invalid_json:
        return "invalid JSON";
    case cgltf_result_invalid_gltf:
        return "invalid glTF";
    case cgltf_result_file_not_found:
        return "file not found";
    case cgltf_result_io_error:
        return "cannot read the file or one of its buffers";
    case cgltf_result_out_of_memory:
        return "out of memory";
    case cgltf_result_legacy_gltf:
        return "glTF 1.0, only 2.0 is read";
    default:
        return "error";
    }
}

class GltfReader {
public:
    GltfReader(std::string file, TriangleMesh& mesh) : file_(std::move(file)), mesh_(mesh) {}

    // Appends the triangles of one primitive, each corner moved by `transform`.
    void addPrimitive(const cgltf_primitive& primitive, const glm::mat4& transform)
    {
        if (primitive.has_draco_mesh_compression) {
            throw std::runtime_error("glTF file '" + file_ + "' uses Draco compression, which is not read");
        }
        if (primitive.type != cgltf_primitive_type_triangles && primitive.type != cgltf_primitive_type_triangle_strip &&
            primitive.type != cgltf_primitive_type_triangle_fan) {
            return; // points and lines have no surface to voxelize
        }
        const cgltf_accessor* positions = nullptr;
        for (cgltf_size i = 0; i < primitive.attributes_count; ++i) {
            if (primitive.attributes[i].type == cgltf_attribute_type_position)
                positions = primitive.attributes[i].data;
        }
        if (positions == nullptr || positions->type != cgltf_type_vec3) {
            throw std::runtime_error("glTF file '" + file_ + "': a triangle primitive has no positions");
        }

        const auto first = static_cast<std::uint32_t>(mesh_.positions.size());
        std::vector<float> values(positions->count * 3);
        if (cgltf_accessor_unpack_floats(positions, values.data(), values.size()) != values.size()) {
            throw std::runtime_error("glTF file '" + file_ + "': cannot read the positions of a primitive");
        }
        for (cgltf_size i = 0; i < positions->count; ++i) {
            const glm::vec4 moved = transform * glm::vec4(values[3 * i], values[3 * i + 1], values[3 * i + 2], 1.0f);
            mesh_.positions.emplace_back(moved);
        }

        // The corners in order: the index list, or every vertex once.
        const cgltf_size cornerCount = primitive.indices ? primitive.indices->count : positions->count;
        std::vector<std::uint32_t> corners(cornerCount);
        for (cgltf_size i = 0; i < cornerCount; ++i) {
            const cgltf_size index = primitive.indices ? cgltf_accessor_read_index(primitive.indices, i) : i;
            if (index >= positions->count) {
                throw std::runtime_error("glTF file '" + file_ + "': a primitive names a vertex that does not exist");
            }
            corners[i] = first + static_cast<std::uint32_t>(index);
        }
        const auto add = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c) {
            mesh_.indices.insert(mesh_.indices.end(), {a, b, c});
        };
        if (primitive.type == cgltf_primitive_type_triangles) {
            for (cgltf_size i = 0; i + 2 < cornerCount; i += 3)
                add(corners[i], corners[i + 1], corners[i + 2]);
        } else if (primitive.type == cgltf_primitive_type_triangle_strip) {
            // Every second triangle of a strip turns the other way; swap two corners to keep the winding.
            for (cgltf_size i = 0; i + 2 < cornerCount; ++i) {
                if (i % 2 == 0)
                    add(corners[i], corners[i + 1], corners[i + 2]);
                else
                    add(corners[i + 1], corners[i], corners[i + 2]);
            }
        } else {
            for (cgltf_size i = 1; i + 1 < cornerCount; ++i)
                add(corners[0], corners[i], corners[i + 1]);
        }
    }

    void addMesh(const cgltf_mesh& mesh, const glm::mat4& transform)
    {
        for (cgltf_size i = 0; i < mesh.primitives_count; ++i)
            addPrimitive(mesh.primitives[i], transform);
    }

    // A node and its children, each placed by its transform in the world.
    void addNode(const cgltf_node& node)
    {
        if (node.mesh != nullptr) {
            glm::mat4 world;
            cgltf_node_transform_world(&node, &world[0][0]); // column major, as glm
            addMesh(*node.mesh, world);
        }
        for (cgltf_size i = 0; i < node.children_count; ++i)
            addNode(*node.children[i]);
    }

private:
    std::string file_;
    TriangleMesh& mesh_;
};

} // namespace

TriangleMesh loadGltf(const std::filesystem::path& path)
{
    const std::string file = path.string();
    const cgltf_options options{};
    cgltf_data* parsed = nullptr;
    cgltf_result result = cgltf_parse_file(&options, file.c_str(), &parsed);
    const std::unique_ptr<cgltf_data, FreeData> data(parsed);
    if (result == cgltf_result_success)
        result = cgltf_load_buffers(&options, data.get(), file.c_str());
    if (result == cgltf_result_success)
        result = cgltf_validate(data.get());
    if (result != cgltf_result_success) {
        throw std::runtime_error("Cannot read glTF file '" + file + "': " + resultName(result));
    }
    for (cgltf_size i = 0; i < data->extensions_required_count; ++i) {
        const char* extension = data->extensions_required[i];
        if (std::strcmp(extension, "KHR_mesh_quantization") != 0) {
            throw std::runtime_error("glTF file '" + file + "' needs extension " + extension + ", which is not read");
        }
    }

    TriangleMesh mesh;
    mesh.name = path.stem().string();
    GltfReader reader(file, mesh);
    // The default scene, else the first. A file without scenes has no placement for its meshes:
    // each mesh is taken once, as it is stored.
    const cgltf_scene* scene = data->scene != nullptr ? data->scene : data->scenes_count > 0 ? data->scenes : nullptr;
    if (scene != nullptr) {
        for (cgltf_size i = 0; i < scene->nodes_count; ++i)
            reader.addNode(*scene->nodes[i]);
    } else {
        for (cgltf_size i = 0; i < data->meshes_count; ++i)
            reader.addMesh(data->meshes[i], glm::mat4(1.0f));
    }
    if (mesh.indices.empty()) {
        throw std::runtime_error("glTF file '" + file + "' holds no triangles");
    }
    return mesh;
}

} // namespace thesis::mesh
