#include "thesis/tracers/software_grid/UniformGrid.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace thesis::tracers {
namespace {

constexpr float kCellsPerPrimitive = 4.0f; // density of a frame without a hint

std::string sizeText(const glm::uvec3& size)
{
    return std::to_string(size.x) + " x " + std::to_string(size.y) + " x " + std::to_string(size.z);
}

} // namespace

GridFrame chooseGridFrame(std::size_t primitiveCount, const core::Bounds& bounds,
                          const std::optional<core::RegularGrid>& hint)
{
    if (primitiveCount == 0 || bounds.empty()) {
        throw std::invalid_argument("chooseGridFrame: the scene has no primitives");
    }

    GridFrame frame;
    if (hint) {
        frame.origin = hint->origin;
        frame.cellSize = glm::vec3(hint->cellSize);
        frame.size = hint->size;
        frame.oneCellEach = true;
        if (std::min({frame.size.x, frame.size.y, frame.size.z}) == 0 ||
            std::max({frame.size.x, frame.size.y, frame.size.z}) > kMaxCellsPerAxis) {
            throw std::length_error("A grid of " + sizeText(frame.size) + " cells is outside the supported size");
        }
        return frame;
    }

    // About kCellsPerPrimitive cells for each primitive, as cubic as the bounds allow.
    const glm::vec3 extent = bounds.hi - bounds.lo;
    const float longest = std::max({extent.x, extent.y, extent.z, 1e-6f});
    // A little larger than the bounds, so that a box on the border is inside. An axis with no
    // extent (a flat scene) gets one thin layer of cells.
    const glm::vec3 padded = glm::max(extent, glm::vec3(1e-3f * longest)) * 1.001f;
    const float volume = padded.x * padded.y * padded.z;
    const float cellsPerLength = std::cbrt(kCellsPerPrimitive * static_cast<float>(primitiveCount) / volume);

    frame.origin = bounds.lo - 0.5f * (padded - extent);
    for (int axis = 0; axis < 3; ++axis) {
        const float cells = std::ceil(padded[axis] * cellsPerLength);
        frame.size[axis] = static_cast<std::uint32_t>(std::clamp(cells, 1.0f, static_cast<float>(kMaxCellsPerAxis)));
        frame.cellSize[axis] = padded[axis] / static_cast<float>(frame.size[axis]);
    }
    return frame;
}

UniformGrid buildUniformGrid(std::span<const core::Aabb> boxes, const GridFrame& frame)
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
