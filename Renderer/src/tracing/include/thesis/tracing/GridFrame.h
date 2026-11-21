#pragma once

// Side: CPU

#include "thesis/core/GpuTypes.h"
#include "thesis/core/Scene.h"

#include <glm/common.hpp>
#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace thesis::tracing {

// Where the cells of a grid are, and which cells a primitive belongs to. Every tracer whose
// structure is made of cells stands on it, so they all put the same primitive into the same cells.
//
// Cell (x, y, z) is the box from origin + cellSize * (x, y, z) to origin + cellSize * (x + 1, y + 1, z + 1).
struct GridFrame {
    glm::vec3 origin{0.0f};
    glm::vec3 cellSize{1.0f};
    glm::uvec3 size{1}; // cells along each axis
    // True when every primitive lies inside one cell (the scene gave a hint). The primitive then
    // goes into the cell that holds its centre. Otherwise it goes into every cell that its box touches.
    bool oneCellEach = false;

    [[nodiscard]] std::uint64_t cellCount() const { return static_cast<std::uint64_t>(size.x) * size.y * size.z; }

    [[nodiscard]] glm::uvec3 cellOf(const glm::vec3& point) const
    {
        const glm::vec3 position = glm::floor((point - origin) / cellSize);
        return glm::uvec3(glm::clamp(position, glm::vec3(0.0f), glm::vec3(size) - 1.0f));
    }

    // Calls visit(x, y, z) for every cell that `box` belongs to.
    template <typename Visit>
    void forEachCellOf(const core::Aabb& box, const Visit& visit) const
    {
        const glm::uvec3 first = oneCellEach ? cellOf(0.5f * (box.lo + box.hi)) : cellOf(box.lo);
        const glm::uvec3 last = oneCellEach ? first : cellOf(box.hi);
        for (std::uint32_t z = first.z; z <= last.z; ++z) {
            for (std::uint32_t y = first.y; y <= last.y; ++y) {
                for (std::uint32_t x = first.x; x <= last.x; ++x) {
                    visit(x, y, z);
                }
            }
        }
    }
};

// The most cells along one axis of any grid frame.
inline constexpr std::uint32_t kMaxCellsPerAxis = 1024;

// Chooses the frame from the boxes of the primitives, the same input that the GPU gets for its
// BVH. The one thing more is `hint`: a scene can say that every primitive lies inside one cell
// of a regular grid (the bricks of a voxel model do).
//
// With `hint` the frame is the hint. Without it, the frame covers `bounds` with about four
// cells for each primitive. Throws std::length_error if an axis needs more than kMaxCellsPerAxis cells.
[[nodiscard]] GridFrame chooseGridFrame(std::size_t primitiveCount, const core::Bounds& bounds,
                                        const std::optional<core::RegularGrid>& hint);

} // namespace thesis::tracing
