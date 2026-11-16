#include "thesis/mesh/TriangleMesh.h"

// ============================================================================
// READING ORDER: FILE 1B (Reading Stanford .ply 3D scan files with miniply)
// ============================================================================

#include <miniply.h>

#include <stdexcept>

namespace thesis::mesh {

// miniply writes three floats per vertex straight into the positions array.
// `static_assert` guarantees at compile time that `glm::vec3` has no hidden padding bytes.
static_assert(sizeof(glm::vec3) == 3 * sizeof(float));

TriangleMesh loadPly(const std::filesystem::path& path)
{
    const std::string file = path.string();
    miniply::PLYReader reader(file.c_str());
    if (!reader.valid()) {
        throw std::runtime_error("Cannot read PLY file '" + file + "'");
    }

    TriangleMesh mesh;
    // `path.stem()` strips the folder and `.ply` extension (e.g., `../Models/lucy.ply` -> `"lucy"`).
    mesh.name = path.stem().string();
    bool gotPositions = false;
    bool gotFaces = false;

    // A PLY file is a sequence of "elements" (usually a `vertex` table followed by a `face` table).
    // We step through the elements until we have read both vertices and faces.
    while (reader.has_element() && !(gotPositions && gotFaces)) {
        std::uint32_t properties[3];
        if (reader.element_is(miniply::kPLYVertexElement) && reader.load_element() && reader.find_pos(properties)) {
            mesh.positions.resize(reader.num_rows());
            reader.extract_properties(properties, 3, miniply::PLYPropertyType::Float, mesh.positions.data());
            gotPositions = true;
        } else if (reader.element_is(miniply::kPLYFaceElement) && reader.load_element() &&
                   reader.find_indices(properties)) {
            if (!gotPositions) {
                throw std::runtime_error("PLY file '" + file + "': faces come before vertices, which is not supported");
            }
            const std::uint32_t list = properties[0];
            // If any face has 4+ vertices (e.g., quads), miniply splits it into triangles for us.
            if (reader.requires_triangulation(list)) {
                mesh.indices.resize(static_cast<std::size_t>(reader.num_triangles(list)) * 3);
                reader.extract_triangles(list, &mesh.positions[0].x, static_cast<std::uint32_t>(mesh.positions.size()),
                                         miniply::PLYPropertyType::UInt, mesh.indices.data());
            } else {
                mesh.indices.resize(static_cast<std::size_t>(reader.num_rows()) * 3);
                reader.extract_list_property(list, miniply::PLYPropertyType::UInt, mesh.indices.data());
            }
            gotFaces = true;
        }
        if (!(gotPositions && gotFaces)) {
            reader.next_element();
        }
    }

    if (!gotPositions || !gotFaces || mesh.indices.empty()) {
        throw std::runtime_error("PLY file '" + file + "' holds no triangles");
    }
    // Sanity check: make sure no triangle index points past the end of `mesh.positions`.
    for (const std::uint32_t index : mesh.indices) {
        if (index >= mesh.positions.size()) {
            throw std::runtime_error("PLY file '" + file + "': a face names a vertex that does not exist");
        }
    }
    return mesh;
}

} // namespace thesis::mesh
