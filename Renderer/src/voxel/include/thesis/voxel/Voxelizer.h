#pragma once

// ============================================================================
// READING ORDER: FILE 4A (Step 1 Offline Pipeline: Turning Triangles into Voxels)
// ============================================================================
// VOXELIZATION BASICS: How do you turn a surface made of triangles into 3D voxels?
// There are two rules implemented here:
//   1. `Sampled` (default, from the 2012 Reichl et al. paper):
//      Imagine shooting a grid of parallel laser lines along X, along Y, and along Z through the
//      center of every column of voxels `(i + 0.5, j + 0.5)`. Wherever a line pierces a triangle,
//      we fill the single voxel containing that intersection point.
//      WHY? Because it creates a very thin shell (almost every column has at most 1 voxel!),
//      which compresses into `HEIGHT` cells in the 2012 format instead of needing multi-voxel runs,
//      while still guaranteeing no axis-aligned gaps ("watertight" against flood-fill).
//   2. `Conservative`:
//      Fills EVERY voxel cube that touches any part of a triangle (tested with the 3D Separating
//      Axis Theorem). This produces a thicker shell where tilted surfaces touch 2 or 3 vertically
//      stacked voxels per column.

#include "thesis/mesh/TriangleMesh.h"
#include "thesis/voxel/VoxelModel.h"

#include <cstdint>
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

// Turns a triangle mesh into voxels. The mesh is scaled so that its longest side spans
// `resolution` voxels, with half a voxel of margin on each side.
//
// Each filled voxel gets the average normal of the triangles that filled it. The work is done
// one brick at a time and in parallel, so memory use stays near the size of the result.
// The result does not depend on the number of threads.
[[nodiscard]] VoxelModel voxelize(const mesh::TriangleMesh& mesh, const VoxelizeOptions& options,
                                  VoxelizeReport* report = nullptr);

} // namespace thesis::voxel
