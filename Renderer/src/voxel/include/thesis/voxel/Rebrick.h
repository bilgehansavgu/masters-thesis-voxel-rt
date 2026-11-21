#pragma once

// Side: CPU

#include "thesis/voxel/VoxelModel.h"

#include <cstdint>

namespace thesis::voxel {

// The same voxels with the same normals, grouped into bricks of `brickSize` (8, 16 or 32).
//
// The grid keeps its origin and its voxel size. Its size grows to the next multiple of
// `brickSize` if it is not one already. The result keeps the rules of VoxelModel: only bricks
// with a filled voxel, in ascending order, normals in ascending bit order.
//
// This is how one voxel file gives the same model with several brick sizes: the brick is the
// box that a tracer gets for its search structure, so the brick size is the box size.
// Throws std::invalid_argument for a brick size that is not supported.
[[nodiscard]] VoxelModel rebrick(const VoxelModel& model, std::uint32_t brickSize);

} // namespace thesis::voxel
