#pragma once

// ============================================================================
// READING ORDER: FILE 5 (Saving & loading `.voxels` files on disk)
// ============================================================================
// WHY DO WE SAVE UNCOMPRESSED VOXELS TO DISK INSTEAD OF ENCODED BRICKS?
// Voxelizing a huge mesh like Lucy at 8192 resolution takes several seconds, whereas encoding
// those voxels into `brick2012_inline` or `brick2012_split` takes ~1.5s.
// By saving the plain `VoxelModel` to a `.voxels` file once (`thesis_model voxelize`), the
// renderer can load that `.voxels` file and switch between different encodings live in the window
// (pressing `E`), and `thesis_model report` can compare all encodings on the exact same voxels!

#include "thesis/voxel/VoxelModel.h"

#include <filesystem>

namespace thesis::voxel {

// The voxel file (.voxels): a voxel model on disk, so that a mesh is voxelized only once.
//
// Layout: 8 bytes "THVOXEL1", an 8-byte length, a JSON header of that length, then raw sections.
// The header names the grid, the source, and each section with its offset and size, so the file
// can be inspected with a text editor. Numbers are little endian.
void saveVoxelModel(const std::filesystem::path& path, const VoxelModel& model);
// Throws std::runtime_error if the file is missing, damaged, or of another version.
[[nodiscard]] VoxelModel loadVoxelModel(const std::filesystem::path& path);

} // namespace thesis::voxel
