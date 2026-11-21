#pragma once

// Side: CPU

#include "thesis/voxel/VoxelModel.h"

#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace thesis::brick2012 {

// A row of filled voxels in one column: `length` voxels starting at height `start`.
struct Run {
    std::uint8_t start = 0;
    std::uint8_t length = 0;

    bool operator==(const Run&) const = default;
};

// One brick as columns along its primary axis: the 2012 format before any bit layout.
//
// The primary grid has N x N cells (N = brickSize). Cell (u, v), index u + N * v, is the column
// of voxels along `axis` at position u on axis (axis + 1) % 3 and v on axis (axis + 2) % 3.
// A column is stored as its runs in ascending height, and one normal per filled voxel in
// ascending height. Both bit layouts and the statistics are made from this form.
struct ColumnBrick {
    std::uint32_t brickSize = 0;
    std::uint32_t axis = 0; // primary axis: 0 = x, 1 = y, 2 = z

    [[nodiscard]] std::uint32_t cellCount() const { return brickSize * brickSize; }
    [[nodiscard]] std::span<const Run> runs(std::uint32_t cell) const;
    [[nodiscard]] std::span<const std::uint16_t> normals(std::uint32_t cell) const;

    // Adds the next cell, in cell order. Call exactly cellCount() times.
    void appendCell(std::span<const Run> cellRuns, std::span<const std::uint16_t> cellNormals);

private:
    std::vector<std::uint32_t> runEnd_;    // per cell: index one past its last run
    std::vector<Run> runs_;                // all columns, in cell order
    std::vector<std::uint32_t> normalEnd_; // per cell: index one past its last normal
    std::vector<std::uint16_t> normals_;   // all columns, in cell order
};

// Position inside the brick of the voxel at `height` in cell (u, v).
[[nodiscard]] glm::uvec3 voxelOfColumn(std::uint32_t axis, std::uint32_t u, std::uint32_t v, std::uint32_t height);

// The rule of Reichl et al. 2012: the axis closest to the average normal of the brick. Here: the
// axis along which the normals of the brick point the most (largest sum of |component|).
[[nodiscard]] std::uint32_t choosePrimaryAxis(const voxel::VoxelBrick& brick);

[[nodiscard]] ColumnBrick toColumns(const voxel::VoxelBrick& brick, std::uint32_t brickSize, std::uint32_t axis);
[[nodiscard]] voxel::VoxelBrick fromColumns(const ColumnBrick& columns, const glm::uvec3& coordinate);

// Counts that decide how compact a bit layout can be. The same for every layout.
struct ColumnStatistics {
    std::uint64_t bricks = 0;
    std::uint64_t voxels = 0;
    std::uint64_t emptyCells = 0;
    std::uint64_t heightCells = 0;                      // exactly one voxel in the column
    std::uint64_t pointerCells = 0;                     // more than one voxel in the column
    std::array<std::uint64_t, 4> pointerCellsByRuns{};  // 1, 2, 3, 4 or more runs
    std::array<std::uint64_t, 5> pointerRunsByLength{}; // length 1, 2, 3, 4 to 7, 8 or more
    std::uint64_t pointerVoxels = 0;                    // voxels in pointer cells
    std::array<std::uint64_t, 3> bricksByAxis{};        // primary axis x, y, z

    void add(const ColumnBrick& columns);
    void add(const ColumnStatistics& other);
    [[nodiscard]] nlohmann::json toJson() const;
};

// How much the choice of the primary axis matters: the number of pointer cells with the rule
// of the paper, and with the best axis of each brick (the one with the fewest pointer cells).
struct AxisChoiceStatistics {
    std::uint64_t pointerCellsPaperRule = 0;
    std::uint64_t pointerCellsBestAxis = 0;
    std::uint64_t bricksWhereBestDiffers = 0;

    [[nodiscard]] nlohmann::json toJson() const;
};

[[nodiscard]] AxisChoiceStatistics axisChoiceStatistics(const voxel::VoxelModel& model);

} // namespace thesis::brick2012
