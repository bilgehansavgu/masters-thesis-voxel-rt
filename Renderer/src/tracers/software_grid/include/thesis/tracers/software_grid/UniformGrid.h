#pragma once

// Side: CPU

#include "thesis/core/GpuTypes.h"
#include "thesis/tracing/GridFrame.h"

#include <cstdint>
#include <span>
#include <vector>

namespace thesis::tracers {

// The search structure of the grid tracer: a uniform grid. It is a 3D array of equal cells over
// the scene. Each cell lists the primitives that belong to it.
//
// The list of cell c is items[cellStart[c] .. cellStart[c + 1]), with
// c = x + size.x * (y + size.y * z). trace_software_grid.slang walks it.
struct UniformGrid {
    tracing::GridFrame frame;
    std::vector<std::uint32_t> cellStart; // one entry for each cell and one more
    std::vector<std::uint32_t> items;     // primitive indices
};

// The most cells a uniform grid may have. buildUniformGrid throws std::length_error above it.
// A grid stores every cell, also the empty ones, so its memory grows with the volume of the scene.
inline constexpr std::uint64_t kMaxGridCells = 1ull << 26;

[[nodiscard]] UniformGrid buildUniformGrid(std::span<const core::Aabb> boxes, const tracing::GridFrame& frame);

} // namespace thesis::tracers
