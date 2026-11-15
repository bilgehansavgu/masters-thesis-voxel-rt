#include "thesis/voxel/VoxelModel.h"

#include <stdexcept>

namespace thesis::voxel {

std::uint64_t VoxelModel::voxelCount() const
{
    std::uint64_t count = 0;
    for (const VoxelBrick& brick : bricks) {
        count += brick.normals.size();
    }
    return count;
}

void VoxelModel::validate() const
{
    const auto fail = [&](const std::string& what) { throw std::runtime_error("Voxel model '" + name + "': " + what); };

    const std::uint32_t n = grid.brickSize;
    if (!isSupportedBrickSize(n)) {
        fail("brick size " + std::to_string(n) + " is not supported (8, 16 or 32)");
    }
    if (grid.voxelSize <= 0.0f) {
        fail("voxel size must be positive");
    }
    for (int axis = 0; axis < 3; ++axis) {
        if (grid.size[axis] == 0 || grid.size[axis] % n != 0) {
            fail("grid size must be a positive multiple of the brick size");
        }
    }

    const glm::uvec3 bricksPerAxis = grid.size / n;
    const std::size_t words = voxelsPerBrick(n) / 64;
    bool first = true;
    std::uint64_t previous = 0;
    for (const VoxelBrick& brick : bricks) {
        for (int axis = 0; axis < 3; ++axis) {
            if (brick.coordinate[axis] >= bricksPerAxis[axis]) {
                fail("a brick lies outside the grid");
            }
        }
        const std::uint64_t order =
            brick.coordinate.x +
            static_cast<std::uint64_t>(bricksPerAxis.x) *
                (brick.coordinate.y + static_cast<std::uint64_t>(bricksPerAxis.y) * brick.coordinate.z);
        if (!first && order <= previous) {
            fail("bricks are not in ascending order, or one brick is stored twice");
        }
        first = false;
        previous = order;

        if (brick.occupancy.size() != words) {
            fail("a brick has the wrong number of occupancy bits");
        }
        const std::uint32_t filled = filledCount(brick.occupancy);
        if (filled == 0) {
            fail("an empty brick is stored");
        }
        if (brick.normals.size() != filled) {
            fail("a brick does not have one normal per filled voxel");
        }
    }
}

} // namespace thesis::voxel
