#include "CubeByCubeTracer.h"

#include "thesis/shared/Hit.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <execution>
#include <limits>
#include <numeric>

namespace thesis::tests {

CubeByCubeTracer::CubeByCubeTracer(const voxel::VoxelModel& model) : voxelSize_(model.grid.voxelSize)
{
    const std::uint32_t n = model.grid.brickSize;
    const glm::dvec3 origin(model.grid.origin);
    for (std::size_t index = 0; index < model.bricks.size(); ++index) {
        const voxel::VoxelBrick& brick = model.bricks[index];
        std::size_t next = 0; // the normals are stored in ascending bit order: x fastest, then y, then z
        for (std::uint32_t z = 0; z < n; ++z) {
            for (std::uint32_t y = 0; y < n; ++y) {
                for (std::uint32_t x = 0; x < n; ++x) {
                    if (!voxel::isFilled(brick.occupancy, voxel::voxelIndex(n, x, y, z))) {
                        continue;
                    }
                    const glm::dvec3 position(brick.coordinate * n + glm::uvec3(x, y, z));
                    cubes_.push_back(
                        {origin + voxelSize_ * position, static_cast<std::uint32_t>(index), brick.normals.at(next++)});
                }
            }
        }
    }
}

core::Hit CubeByCubeTracer::trace(const core::Ray& ray) const
{
    const glm::dvec3 origin(ray.origin);
    const glm::dvec3 direction(ray.direction);
    core::Hit best{ray.tMax, core::kNoHit, 0, 0};
    double bestT = std::numeric_limits<double>::max();

    for (const Cube& cube : cubes_) {
        // Slab test against the cube. The ray is inside from tEnter to tExit.
        double tEnter = ray.tMin;
        double tExit = ray.tMax;
        std::uint32_t enterAxis = 0;
        bool outside = false;
        for (int axis = 0; axis < 3 && !outside; ++axis) {
            const double lo = cube.lo[axis];
            const double hi = lo + voxelSize_;
            if (direction[axis] == 0.0) {
                outside = origin[axis] < lo || origin[axis] > hi;
                continue;
            }
            const double tA = (lo - origin[axis]) / direction[axis];
            const double tB = (hi - origin[axis]) / direction[axis];
            if (std::min(tA, tB) > tEnter) {
                tEnter = std::min(tA, tB);
                enterAxis = static_cast<std::uint32_t>(axis);
            }
            tExit = std::min(tExit, std::max(tA, tB));
        }
        if (outside || tEnter > tExit || tEnter >= bestT) {
            continue;
        }
        bestT = tEnter;
        best.t = static_cast<float>(tEnter);
        best.primitive = cube.brick;
        best.face = enterAxis * 2 + (direction[static_cast<int>(enterAxis)] > 0.0 ? 0u : 1u);
        best.attributes = THESIS_HIT_HAS_NORMAL | cube.normal;
    }
    return best;
}

std::vector<core::Hit> CubeByCubeTracer::trace(std::span<const core::Ray> rays) const
{
    std::vector<core::Hit> hits(rays.size());
    std::vector<std::size_t> order(rays.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::for_each(std::execution::par, order.begin(), order.end(),
                  [&](std::size_t index) { hits[index] = trace(rays[index]); });
    return hits;
}

} // namespace thesis::tests
