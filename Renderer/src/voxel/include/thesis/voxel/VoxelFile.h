#pragma once

// Side: CPU

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
