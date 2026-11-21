#pragma once

// Side: CPU

#include "thesis/mesh/TriangleMesh.h"
#include "thesis/voxel/VoxelModel.h"

#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <string_view>

namespace thesis::voxel {

// Which voxels count as filled.
enum class VoxelizationRule {
    // The method of Reichl et al. 2012. For each of the three axes, one line runs through the
    // centre of every grid column along that axis. Where a line crosses a triangle, the voxel at
    // that point is filled. The result is the union over the three axes. It is thin (often one
    // voxel per column) and it has no holes for rays.
    Sampled,
    // Every voxel that a triangle touches is filled. Thicker: more voxels per column.
    Conservative,
};

[[nodiscard]] std::string_view ruleName(VoxelizationRule rule);
// Throws std::invalid_argument for an unknown name.
[[nodiscard]] VoxelizationRule ruleFromName(std::string_view name);

struct VoxelizeOptions {
    std::uint32_t resolution = 512; // voxels along the longest axis of the mesh, a multiple of brickSize
    std::uint32_t brickSize = 32;
    VoxelizationRule rule = VoxelizationRule::Sampled;
};

struct VoxelizeReport {
    std::uint64_t triangles = 0;
    std::uint64_t degenerateTriangles = 0; // zero area, skipped
    std::uint64_t bricks = 0;
    std::uint64_t voxels = 0;
    double binSeconds = 0.0;      // sorting triangles into bricks
    double voxelizeSeconds = 0.0; // filling the bricks (all CPU cores)
};

// Lets another thread follow a long voxelize() or importMesh() call and stop it.
struct WorkMonitor {
    std::atomic<float> done{0.0f};   // share of the work done, 0 to 1; written by the worker
    std::atomic<bool> cancel{false}; // set by the other thread; the worker then throws Cancelled
};

// Thrown by voxelize() and importMesh() after WorkMonitor::cancel was set.
class Cancelled : public std::runtime_error {
public:
    Cancelled() : std::runtime_error("Cancelled") {}
};

// Turns a triangle mesh into voxels. The mesh is scaled so that its longest side spans
// `resolution` voxels, with half a voxel of margin on each side.
//
// Each filled voxel gets the average normal of the triangles that filled it. The work is done
// one brick at a time and in parallel, so memory use stays near the size of the result.
// The result does not depend on the number of threads.
//
// `monitor`, if set, gets the share of the work done; if its `cancel` is set, the call throws
// Cancelled within a few bricks of work.
[[nodiscard]] VoxelModel voxelize(const mesh::TriangleMesh& mesh, const VoxelizeOptions& options,
                                  VoxelizeReport* report = nullptr, WorkMonitor* monitor = nullptr);

} // namespace thesis::voxel
