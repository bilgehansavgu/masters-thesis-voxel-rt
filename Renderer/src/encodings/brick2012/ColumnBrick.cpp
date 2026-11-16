// ============================================================================
// READING ORDER: FILE 7B (after ColumnBrick.h)
// ============================================================================
// PURPOSE:
// Implements the conversion between a 3D bit-packed VoxelBrick and the 2D
// N x N column representation (ColumnBrick) used by the 2012 Reichl et al.
// brick format, plus statistics gathering on column types and primary axis
// choices.
// ============================================================================

#include "thesis/brick2012/ColumnBrick.h"

#include "thesis/voxel/OctNormal.h"

#include <glm/common.hpp>

#include <algorithm>
#include <cmath>
#include <execution>
#include <numeric>
#include <stdexcept>

namespace thesis::brick2012 {

// Returns the slice of runs belonging to `cell` using the prefix-sum array `runEnd_`.
std::span<const Run> ColumnBrick::runs(std::uint32_t cell) const
{
    const std::uint32_t begin = cell == 0 ? 0 : runEnd_[cell - 1];
    return std::span<const Run>(runs_).subspan(begin, runEnd_[cell] - begin);
}

// Returns the slice of 16-bit packed normals belonging to `cell`.
std::span<const std::uint16_t> ColumnBrick::normals(std::uint32_t cell) const
{
    const std::uint32_t begin = cell == 0 ? 0 : normalEnd_[cell - 1];
    return std::span<const std::uint16_t>(normals_).subspan(begin, normalEnd_[cell] - begin);
}

// Appends a cell's runs and normals to the flat vectors and records the new end offsets.
void ColumnBrick::appendCell(std::span<const Run> cellRuns, std::span<const std::uint16_t> cellNormals)
{
    runs_.insert(runs_.end(), cellRuns.begin(), cellRuns.end());
    normals_.insert(normals_.end(), cellNormals.begin(), cellNormals.end());
    runEnd_.push_back(static_cast<std::uint32_t>(runs_.size()));
    normalEnd_.push_back(static_cast<std::uint32_t>(normals_.size()));
}

// Converts column coordinates (u, v, height) back into 3D brick-local voxel coordinates (x, y, z):
//   - Primary axis `a` gets `height`
//   - Secondary axis `(a + 1) % 3` gets `u`
//   - Tertiary axis `(a + 2) % 3` gets `v`
glm::uvec3 voxelOfColumn(std::uint32_t axis, std::uint32_t u, std::uint32_t v, std::uint32_t height)
{
    glm::uvec3 voxel(0);
    voxel[static_cast<int>(axis)] = height;
    voxel[static_cast<int>((axis + 1) % 3)] = u;
    voxel[static_cast<int>((axis + 2) % 3)] = v;
    return voxel;
}

// ============================================================================
// PRIMARY AXIS SELECTION (Reichl et al. 2012 paper rule)
// ============================================================================
// Decodes every voxel's normal in the brick, sums the absolute values |nx|, |ny|, |nz|,
// and picks the axis with the largest sum.
// Intuition: if a surface inside the brick faces mostly along +Y or -Y, then rays
// looking down the Y axis see a single-layer heightfield (1 voxel per column = HEIGHT cell)
// rather than a tall wall of voxels (which would require a POINTER cell).
std::uint32_t choosePrimaryAxis(const voxel::VoxelBrick& brick)
{
    glm::vec3 sum(0.0f);
    for (const std::uint16_t packed : brick.normals) {
        sum += glm::abs(voxel::decodeNormal(packed));
    }
    if (sum.x >= sum.y && sum.x >= sum.z) {
        return 0;
    }
    return sum.y >= sum.z ? 1 : 2;
}

// ============================================================================
// 3D VOXEL BRICK -> 2D COLUMN BRICK (`toColumns`)
// ============================================================================
// Walks every column (u, v) from height = 0 to N - 1, groups contiguous filled
// voxels into `Run{start, length}` entries, and gathers their normals.
//
// How do we find a voxel's normal in `brick.normals` in O(1) time?
// Recall from VoxelModel.h that `brick.normals` only stores normals for FILLED voxels,
// ordered by linear voxel index `x + N * (y + N * z)`.
// So the normal for filled voxel `index` is at position:
//   (number of set bits in `brick.occupancy` strictly before `index`).
//
// We compute this in two fast steps:
//   1. Precompute `filledBefore[word]`: a prefix sum of `std::popcount(brick.occupancy[word])`
//      giving the total number of filled voxels in all 64-bit words before `word`.
//   2. For a voxel at bit `(index & 63)` inside `word = index >> 6`, mask out all bits below it
//      using `((1ull << (index & 63)) - 1)` and count them with a single hardware instruction
//      `std::popcount(below)`.
ColumnBrick toColumns(const voxel::VoxelBrick& brick, std::uint32_t brickSize, std::uint32_t axis)
{
    // Number of filled voxels before each 64-bit word: gives the position of a voxel's normal.
    std::vector<std::uint32_t> filledBefore(brick.occupancy.size());
    std::uint32_t running = 0;
    for (std::size_t word = 0; word < brick.occupancy.size(); ++word) {
        filledBefore[word] = running;
        running += static_cast<std::uint32_t>(std::popcount(brick.occupancy[word]));
    }

    ColumnBrick columns;
    columns.brickSize = brickSize;
    columns.axis = axis;
    std::vector<Run> runs;
    std::vector<std::uint16_t> normals;
    for (std::uint32_t v = 0; v < brickSize; ++v) {
        for (std::uint32_t u = 0; u < brickSize; ++u) {
            runs.clear();
            normals.clear();
            bool inRun = false;
            for (std::uint32_t height = 0; height < brickSize; ++height) {
                const glm::uvec3 voxel = voxelOfColumn(axis, u, v, height);
                const std::uint32_t index = voxel::voxelIndex(brickSize, voxel.x, voxel.y, voxel.z);
                if (!voxel::isFilled(brick.occupancy, index)) {
                    inRun = false;
                    continue;
                }
                const std::uint64_t below = brick.occupancy[index >> 6] & ((1ull << (index & 63)) - 1);
                normals.push_back(
                    brick.normals[filledBefore[index >> 6] + static_cast<std::uint32_t>(std::popcount(below))]);
                if (inRun) {
                    // Contiguous with the previous filled voxel in this column: extend the current run.
                    ++runs.back().length;
                } else {
                    // Start of a new contiguous run of filled voxels.
                    runs.push_back({static_cast<std::uint8_t>(height), 1});
                    inRun = true;
                }
            }
            columns.appendCell(runs, normals);
        }
    }
    return columns;
}

// ============================================================================
// 2D COLUMN BRICK -> 3D VOXEL BRICK (`fromColumns`)
// ============================================================================
// The exact inverse of `toColumns`: unpacks all runs and normals from every column
// back into 3D `(x, y, z)` occupancy bits and re-collects `brick.normals` in
// standard linear voxel order (`index = 0 .. N^3 - 1`).
voxel::VoxelBrick fromColumns(const ColumnBrick& columns, const glm::uvec3& coordinate)
{
    const std::uint32_t n = columns.brickSize;
    voxel::VoxelBrick brick;
    brick.coordinate = coordinate;
    brick.occupancy.assign(voxel::voxelsPerBrick(n) / 64, 0);
    std::vector<std::uint16_t> normalOfVoxel(voxel::voxelsPerBrick(n), 0);

    for (std::uint32_t cell = 0; cell < columns.cellCount(); ++cell) {
        const std::span<const std::uint16_t> normals = columns.normals(cell);
        std::size_t next = 0;
        for (const Run& run : columns.runs(cell)) {
            for (std::uint32_t step = 0; step < run.length; ++step) {
                const glm::uvec3 voxel = voxelOfColumn(columns.axis, cell % n, cell / n, run.start + step);
                if (voxel.x >= n || voxel.y >= n || voxel.z >= n || next >= normals.size()) {
                    throw std::runtime_error("fromColumns: a run does not fit its column");
                }
                const std::uint32_t index = voxel::voxelIndex(n, voxel.x, voxel.y, voxel.z);
                voxel::setFilled(brick.occupancy, index);
                normalOfVoxel[index] = normals[next++];
            }
        }
    }
    for (std::uint32_t index = 0; index < voxel::voxelsPerBrick(n); ++index) {
        if (voxel::isFilled(brick.occupancy, index)) {
            brick.normals.push_back(normalOfVoxel[index]);
        }
    }
    return brick;
}

// Tallies how many cells in `columns` are EMPTY (0 voxels), HEIGHT (1 voxel),
// or POINTER (>= 2 voxels), and histograms the number of runs and run lengths.
void ColumnStatistics::add(const ColumnBrick& columns)
{
    ++bricks;
    ++bricksByAxis[columns.axis];
    for (std::uint32_t cell = 0; cell < columns.cellCount(); ++cell) {
        const std::size_t count = columns.normals(cell).size();
        voxels += count;
        if (count == 0) {
            ++emptyCells;
        } else if (count == 1) {
            ++heightCells;
        } else {
            ++pointerCells;
            pointerVoxels += count;
            const std::span<const Run> runs = columns.runs(cell);
            ++pointerCellsByRuns[std::min<std::size_t>(runs.size(), 4) - 1];
            for (const Run& run : runs) {
                const std::size_t bucket = run.length <= 3 ? run.length - 1u : (run.length <= 7 ? 3u : 4u);
                ++pointerRunsByLength[bucket];
            }
        }
    }
}

void ColumnStatistics::add(const ColumnStatistics& other)
{
    bricks += other.bricks;
    voxels += other.voxels;
    emptyCells += other.emptyCells;
    heightCells += other.heightCells;
    pointerCells += other.pointerCells;
    pointerVoxels += other.pointerVoxels;
    for (std::size_t i = 0; i < pointerCellsByRuns.size(); ++i) {
        pointerCellsByRuns[i] += other.pointerCellsByRuns[i];
    }
    for (std::size_t i = 0; i < pointerRunsByLength.size(); ++i) {
        pointerRunsByLength[i] += other.pointerRunsByLength[i];
    }
    for (std::size_t i = 0; i < bricksByAxis.size(); ++i) {
        bricksByAxis[i] += other.bricksByAxis[i];
    }
}

nlohmann::json ColumnStatistics::toJson() const
{
    return {{"bricks", bricks},
            {"voxels", voxels},
            {"cells",
             {{"total", emptyCells + heightCells + pointerCells},
              {"empty", emptyCells},
              {"height", heightCells},
              {"pointer", pointerCells}}},
            {"pointer_cells_by_runs",
             {{"1", pointerCellsByRuns[0]},
              {"2", pointerCellsByRuns[1]},
              {"3", pointerCellsByRuns[2]},
              {"4_or_more", pointerCellsByRuns[3]}}},
            {"pointer_runs_by_length",
             {{"1", pointerRunsByLength[0]},
              {"2", pointerRunsByLength[1]},
              {"3", pointerRunsByLength[2]},
              {"4_to_7", pointerRunsByLength[3]},
              {"8_or_more", pointerRunsByLength[4]}}},
            {"voxels_in_pointer_cells", pointerVoxels},
            {"bricks_by_primary_axis", {{"x", bricksByAxis[0]}, {"y", bricksByAxis[1]}, {"z", bricksByAxis[2]}}}};
}

nlohmann::json AxisChoiceStatistics::toJson() const
{
    return {{"pointer_cells_paper_rule", pointerCellsPaperRule},
            {"pointer_cells_best_axis", pointerCellsBestAxis},
            {"bricks_where_best_axis_differs", bricksWhereBestDiffers}};
}

// ============================================================================
// EVALUATING THE 2012 PAPER'S PRIMARY AXIS HEURISTIC (`axisChoiceStatistics`)
// ============================================================================
// For every brick in parallel (`std::execution::par`), tries all 3 possible primary
// axes (0, 1, 2), counts how many POINTER cells each axis produces, and compares the
// axis chosen by the paper's normal-sum rule (`choosePrimaryAxis`) against the axis
// that actually minimizes POINTER cells.
AxisChoiceStatistics axisChoiceStatistics(const voxel::VoxelModel& model)
{
    struct PerBrick {
        std::uint64_t paper = 0;
        std::uint64_t best = 0;
        bool differs = false;
    };
    std::vector<PerBrick> perBrick(model.bricks.size());
    std::vector<std::size_t> order(model.bricks.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::for_each(std::execution::par, order.begin(), order.end(), [&](std::size_t i) {
        std::array<std::uint64_t, 3> pointerCells{};
        for (std::uint32_t axis = 0; axis < 3; ++axis) {
            const ColumnBrick columns = toColumns(model.bricks[i], model.grid.brickSize, axis);
            for (std::uint32_t cell = 0; cell < columns.cellCount(); ++cell) {
                pointerCells[axis] += columns.normals(cell).size() > 1 ? 1u : 0u;
            }
        }
        const std::uint32_t chosen = choosePrimaryAxis(model.bricks[i]);
        const std::uint64_t best = std::min({pointerCells[0], pointerCells[1], pointerCells[2]});
        perBrick[i] = {pointerCells[chosen], best, pointerCells[chosen] != best};
    });

    AxisChoiceStatistics result;
    for (const PerBrick& brick : perBrick) {
        result.pointerCellsPaperRule += brick.paper;
        result.pointerCellsBestAxis += brick.best;
        result.bricksWhereBestDiffers += brick.differs ? 1u : 0u;
    }
    return result;
}

} // namespace thesis::brick2012
