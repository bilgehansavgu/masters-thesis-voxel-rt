#include "thesis/mesh/TriangleMesh.h"

#include <cmath>
#include <numbers>
#include <stdexcept>

namespace thesis::mesh {

TriangleMesh makeSphere(const glm::vec3& center, float radius, std::uint32_t segments)
{
    if (segments < 3 || radius <= 0.0f) {
        throw std::invalid_argument("makeSphere: needs at least 3 segments and a positive radius");
    }
    const std::uint32_t rings = segments / 2 + 1; // rows of vertices between the two poles

    TriangleMesh mesh;
    mesh.name = "sphere";
    mesh.positions.push_back(center + glm::vec3(0.0f, radius, 0.0f)); // north pole, index 0
    for (std::uint32_t ring = 1; ring < rings; ++ring) {
        const float polar = std::numbers::pi_v<float> * static_cast<float>(ring) / static_cast<float>(rings);
        for (std::uint32_t segment = 0; segment < segments; ++segment) {
            const float around =
                2.0f * std::numbers::pi_v<float> * static_cast<float>(segment) / static_cast<float>(segments);
            mesh.positions.push_back(center + radius * glm::vec3(std::sin(polar) * std::cos(around), std::cos(polar),
                                                                 std::sin(polar) * std::sin(around)));
        }
    }
    mesh.positions.push_back(center - glm::vec3(0.0f, radius, 0.0f)); // south pole, last index
    const auto southPole = static_cast<std::uint32_t>(mesh.positions.size() - 1);
    const auto vertex = [&](std::uint32_t ring, std::uint32_t segment) {
        return 1 + (ring - 1) * segments + segment % segments;
    };

    for (std::uint32_t segment = 0; segment < segments; ++segment) {
        mesh.indices.insert(mesh.indices.end(), {0, vertex(1, segment + 1), vertex(1, segment)});
        mesh.indices.insert(mesh.indices.end(),
                            {southPole, vertex(rings - 1, segment), vertex(rings - 1, segment + 1)});
    }
    for (std::uint32_t ring = 1; ring + 1 < rings; ++ring) {
        for (std::uint32_t segment = 0; segment < segments; ++segment) {
            const std::uint32_t a = vertex(ring, segment);
            const std::uint32_t b = vertex(ring, segment + 1);
            const std::uint32_t c = vertex(ring + 1, segment);
            const std::uint32_t d = vertex(ring + 1, segment + 1);
            mesh.indices.insert(mesh.indices.end(), {a, b, d});
            mesh.indices.insert(mesh.indices.end(), {a, d, c});
        }
    }
    return mesh;
}

} // namespace thesis::mesh
