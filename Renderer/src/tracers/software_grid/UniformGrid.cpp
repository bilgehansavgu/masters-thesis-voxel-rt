#include "thesis/tracers/software_grid/UniformGrid.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace thesis::tracers {
namespace {

std::string sizeText(const glm::uvec3& size)
{
    return std::to_string(size.x) + " x " + std::to_string(size.y) + " x " + std::to_string(size.z);
}

} // namespace

UniformGrid buildUniformGrid(std::span<const core::Aabb> boxes, const tracing::GridFrame& frame)
{
    if (frame.cellCount() > kMaxGridCells) {
        throw std::length_error("A uniform grid of " + sizeText(frame.size) + " cells is too large: it stores every " +
                                "cell, also the empty ones");
    }
    UniformGrid grid;
    grid.frame = frame;
    const auto cellIndex = [&](std::uint32_t x, std::uint32_t y, std::uint32_t z) {
        return static_cast<std::size_t>(x) + frame.size.x * (static_cast<std::size_t>(y) + frame.size.y * z);
    };

    // Counting sort: count the entries of each cell, turn the counts into start positions, fill.
    grid.cellStart.assign(static_cast<std::size_t>(frame.cellCount()) + 1, 0);
    std::uint64_t total = 0;
    for (const core::Aabb& box : boxes) {
        frame.forEachCellOf(box, [&](std::uint32_t x, std::uint32_t y, std::uint32_t z) {
            ++grid.cellStart[cellIndex(x, y, z) + 1];
            ++total;
        });
    }
    if (total > 0xFFFFFFFFull) {
        throw std::length_error("A uniform grid needs more than 32-bit offsets for the lists of its cells");
    }
    for (std::size_t cell = 0; cell + 1 < grid.cellStart.size(); ++cell) {
        grid.cellStart[cell + 1] += grid.cellStart[cell];
    }

    grid.items.resize(static_cast<std::size_t>(total));
    std::vector<std::uint32_t> next(grid.cellStart.begin(), grid.cellStart.end() - 1);
    for (std::size_t primitive = 0; primitive < boxes.size(); ++primitive) {
        frame.forEachCellOf(boxes[primitive], [&](std::uint32_t x, std::uint32_t y, std::uint32_t z) {
            grid.items[next[cellIndex(x, y, z)]++] = static_cast<std::uint32_t>(primitive);
        });
    }
    return grid;
}

} // namespace thesis::tracers
