#pragma once

// Side: CPU

#include "thesis/voxel/VoxelModel.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace thesis::tests {

// One filled voxel with its position in the whole grid.
struct GridVoxel {
    glm::uvec3 position;
    std::uint16_t normal;

    bool operator==(const GridVoxel&) const = default;
};

// Every filled voxel of a model, sorted by z, then y, then x. Two models with different brick
// sizes can be compared through this list.
[[nodiscard]] std::vector<GridVoxel> listVoxels(const voxel::VoxelModel& model);

// A model with `bricksPerAxis` bricks of `brickSize`, filled where `normalAt` returns a normal
// for the voxel position. Bricks that stay empty are left out, as the voxelizer does.
// The default grid does not start at zero and its voxels do not have size 1, so that code which
// forgets the origin or the voxel size fails.
[[nodiscard]] voxel::VoxelModel
makeModel(std::uint32_t brickSize, const glm::uvec3& bricksPerAxis,
          const std::function<std::optional<std::uint16_t>(const glm::uvec3&)>& normalAt,
          const glm::vec3& origin = {-1.0f, 2.0f, 0.5f}, float voxelSize = 0.25f);

} // namespace thesis::tests
