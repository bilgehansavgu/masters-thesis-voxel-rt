#pragma once

// Side: CPU

#include "thesis/voxel/VoxelModel.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace thesis::voxel {

// What the header of a voxel file states, read without the voxels: fast for a file of any size.
struct VoxelFileSummary {
    std::string name;
    VoxelGrid grid;
    VoxelSource source;
    std::uint64_t bricks = 0;
    std::uint64_t voxels = 0;
    std::uint64_t fileBytes = 0; // the size of the whole file
};

// The voxel file (.voxels): a voxel model on disk, so that a mesh is voxelized only once.
//
// Layout: 8 bytes "THVOXEL1", an 8-byte length, a JSON header of that length, then raw sections.
// The header names the grid, the source, and each section with its offset and size, so the file
// can be inspected with a text editor. Numbers are little endian.
void saveVoxelModel(const std::filesystem::path& path, const VoxelModel& model);
// Throws std::runtime_error if the file is missing, damaged, or of another version.
[[nodiscard]] VoxelModel loadVoxelModel(const std::filesystem::path& path);
// Reads only the header. Throws std::runtime_error as loadVoxelModel() does for a damaged header.
[[nodiscard]] VoxelFileSummary readVoxelFileSummary(const std::filesystem::path& path);

} // namespace thesis::voxel
