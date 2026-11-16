#include "thesis/tracers/software/UniformGrid.h"

#include <glm/common.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace thesis::tracers {
namespace {

constexpr float kCellsPerPrimitive = 4.0f; // density of a grid without a hint
constexpr std::uint32_t kMaxCellsPerAxis = 1024;

// The cells that a box touches: from `first` to `last`, both included.
struct CellRange {
    glm::uvec3 first;
    glm::uvec3 last;
};

glm::uvec3 cellOf(const UniformGrid& grid, const glm::vec3& point)
{
    const glm::vec3 position = glm::floor((point - grid.origin) / grid.cellSize);
    return glm::uvec3(glm::clamp(position, glm::vec3(0.0f), glm::vec3(grid.size) - 1.0f));
}

CellRange cellsOf(const UniformGrid& grid, const core::Aabb& box, bool oneCellEach)
{
    if (oneCellEach) {
        const glm::uvec3 cell = cellOf(grid, 0.5f * (box.lo + box.hi));
        return {cell, cell};
    }
    return {cellOf(grid, box.lo), cellOf(grid, box.hi)};
}

// A grid over `bounds` with about kCellsPerPrimitive cells for each primitive, as cubic as the
// bounds allow.
void sizeFromBounds(UniformGrid& grid, const core::Bounds& bounds, std::size_t primitiveCount)
{
    const glm::vec3 extent = bounds.hi - bounds.lo;
    const float longest = std::max({extent.x, extent.y, extent.z, 1e-6f});
    // A little larger than the bounds, so that a box on the border is inside. An axis with no
    // extent (a flat scene) gets one thin layer of cells.
    const glm::vec3 padded = glm::max(extent, glm::vec3(1e-3f * longest)) * 1.001f;
    const float volume = padded.x * padded.y * padded.z;
    const float cellsPerLength = std::cbrt(kCellsPerPrimitive * static_cast<float>(primitiveCount) / volume);

    grid.origin = bounds.lo - 0.5f * (padded - extent);
    for (int axis = 0; axis < 3; ++axis) {
        const float cells = std::ceil(padded[axis] * cellsPerLength);
        grid.size[axis] = static_cast<std::uint32_t>(std::clamp(cells, 1.0f, static_cast<float>(kMaxCellsPerAxis)));
        grid.cellSize[axis] = padded[axis] / static_cast<float>(grid.size[axis]);
    }
}

} // namespace

UniformGrid buildUniformGrid(std::span<const core::Aabb> boxes, const core::Bounds& bounds,
                             const std::optional<core::RegularGrid>& hint)
{
    if (boxes.empty() || bounds.empty()) {
        throw std::invalid_argument("buildUniformGrid: the scene has no primitives");
    }

    UniformGrid grid;
    if (hint) {
        grid.origin = hint->origin;
        grid.cellSize = glm::vec3(hint->cellSize);
        grid.size = hint->size;
    } else {
        sizeFromBounds(grid, bounds, boxes.size());
    }
    if (grid.size.x == 0 || grid.size.y == 0 || grid.size.z == 0 || grid.cellCount() > kMaxGridCells) {
        throw std::length_error("buildUniformGrid: a grid of " + std::to_string(grid.size.x) + " x " +
                                std::to_string(grid.size.y) + " x " + std::to_string(grid.size.z) +
                                " cells is outside the supported size");
    }
    const bool oneCellEach = hint.has_value();
    const auto cellIndex = [&](std::uint32_t x, std::uint32_t y, std::uint32_t z) {
        return static_cast<std::size_t>(x) + grid.size.x * (static_cast<std::size_t>(y) + grid.size.y * z);
    };
    const auto forEachCell = [&](const core::Aabb& box, const auto& visit) {
        const CellRange range = cellsOf(grid, box, oneCellEach);
        for (std::uint32_t z = range.first.z; z <= range.last.z; ++z) {
            for (std::uint32_t y = range.first.y; y <= range.last.y; ++y) {
                for (std::uint32_t x = range.first.x; x <= range.last.x; ++x) {
                    visit(cellIndex(x, y, z));
                }
            }
        }
    };

    // Counting sort: count the entries of each cell, turn the counts into start positions, fill.
    grid.cellStart.assign(static_cast<std::size_t>(grid.cellCount()) + 1, 0);
    std::uint64_t total = 0;
    for (const core::Aabb& box : boxes) {
        forEachCell(box, [&](std::size_t cell) {
            ++grid.cellStart[cell + 1];
            ++total;
        });
    }
    if (total > 0xFFFFFFFFull) {
        throw std::length_error("buildUniformGrid: the lists of the cells need more than 32-bit offsets");
    }
    for (std::size_t cell = 0; cell + 1 < grid.cellStart.size(); ++cell) {
        grid.cellStart[cell + 1] += grid.cellStart[cell];
    }

    grid.items.resize(static_cast<std::size_t>(total));
    std::vector<std::uint32_t> next(grid.cellStart.begin(), grid.cellStart.end() - 1);
    for (std::size_t primitive = 0; primitive < boxes.size(); ++primitive) {
        forEachCell(boxes[primitive],
                    [&](std::size_t cell) { grid.items[next[cell]++] = static_cast<std::uint32_t>(primitive); });
    }
    return grid;
}

} // namespace thesis::tracers
