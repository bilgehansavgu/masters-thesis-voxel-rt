#include "VoxelTestHelpers.h"

#include <algorithm>
#include <tuple>

namespace thesis::tests {

std::vector<GridVoxel> listVoxels(const voxel::VoxelModel& model)
{
    const std::uint32_t n = model.grid.brickSize;
    std::vector<GridVoxel> voxels;
    for (const voxel::VoxelBrick& brick : model.bricks) {
        std::size_t next = 0;
        for (std::uint32_t z = 0; z < n; ++z) {
            for (std::uint32_t y = 0; y < n; ++y) {
                for (std::uint32_t x = 0; x < n; ++x) {
                    if (voxel::isFilled(brick.occupancy, voxel::voxelIndex(n, x, y, z))) {
                        voxels.push_back({brick.coordinate * n + glm::uvec3(x, y, z), brick.normals.at(next++)});
                    }
                }
            }
        }
    }
    std::sort(voxels.begin(), voxels.end(), [](const GridVoxel& a, const GridVoxel& b) {
        return std::tie(a.position.z, a.position.y, a.position.x) < std::tie(b.position.z, b.position.y, b.position.x);
    });
    return voxels;
}

voxel::VoxelModel makeModel(std::uint32_t brickSize, const glm::uvec3& bricksPerAxis,
                            const std::function<std::optional<std::uint16_t>(const glm::uvec3&)>& normalAt)
{
    voxel::VoxelModel model;
    model.name = "test model";
    model.grid.origin = {-1.0f, 2.0f, 0.5f};
    model.grid.voxelSize = 0.25f;
    model.grid.brickSize = brickSize;
    model.grid.size = bricksPerAxis * brickSize;

    for (std::uint32_t bz = 0; bz < bricksPerAxis.z; ++bz) {
        for (std::uint32_t by = 0; by < bricksPerAxis.y; ++by) {
            for (std::uint32_t bx = 0; bx < bricksPerAxis.x; ++bx) {
                voxel::VoxelBrick brick;
                brick.coordinate = {bx, by, bz};
                brick.occupancy.assign(voxel::voxelsPerBrick(brickSize) / 64, 0);
                for (std::uint32_t z = 0; z < brickSize; ++z) {
                    for (std::uint32_t y = 0; y < brickSize; ++y) {
                        for (std::uint32_t x = 0; x < brickSize; ++x) {
                            const auto normal = normalAt(brick.coordinate * brickSize + glm::uvec3(x, y, z));
                            if (normal) {
                                voxel::setFilled(brick.occupancy, voxel::voxelIndex(brickSize, x, y, z));
                                brick.normals.push_back(*normal);
                            }
                        }
                    }
                }
                if (!brick.normals.empty()) {
                    model.bricks.push_back(std::move(brick));
                }
            }
        }
    }
    return model;
}

} // namespace thesis::tests
