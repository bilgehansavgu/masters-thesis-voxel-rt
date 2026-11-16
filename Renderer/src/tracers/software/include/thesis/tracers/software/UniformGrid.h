#pragma once

#include "thesis/core/GpuTypes.h"
#include "thesis/core/Scene.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace thesis::tracers {

// The search structure of the software tracer: a uniform grid. It is a 3D array of equal cells
// over the scene. Each cell lists the primitives whose boxes touch it.
//
// The list of cell c is items[cellStart[c] .. cellStart[c + 1]), with
// c = x + size.x * (y + size.y * z). trace_software.slang walks it.
struct UniformGrid {
    glm::vec3 origin{0.0f}; // corner of cell (0, 0, 0)
    glm::vec3 cellSize{1.0f};
    glm::uvec3 size{1};
    std::vector<std::uint32_t> cellStart; // one entry for each cell and one more
    std::vector<std::uint32_t> items;     // primitive indices

    [[nodiscard]] std::uint64_t cellCount() const { return static_cast<std::uint64_t>(size.x) * size.y * size.z; }
};

// The most cells a grid may have. buildUniformGrid throws std::length_error above it.
inline constexpr std::uint64_t kMaxGridCells = 1ull << 26;

// Builds the grid from the boxes of the primitives, the same input that the GPU gets for its
// BVH. The one thing more is `hint`: a scene can say that every primitive lies inside one cell of
// a regular grid (the bricks of a voxel model do).
//
// With `hint` the grid is the hint, and each primitive goes into the cell that holds its centre.
// Without it, the grid covers `bounds` with about four cells for each primitive, and each
// primitive goes into every cell that its box touches.
[[nodiscard]] UniformGrid buildUniformGrid(std::span<const core::Aabb> boxes, const core::Bounds& bounds,
                                           const std::optional<core::RegularGrid>& hint);

} // namespace thesis::tracers
