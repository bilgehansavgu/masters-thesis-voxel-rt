#pragma once

// Side: CPU

#include "thesis/voxel/Voxelizer.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace thesis::voxel {

// The axis of the mesh that points up. The renderer treats +y as up; scans such as Lucy use +z.
enum class UpAxis {
    Y,
    Z,
};

[[nodiscard]] std::string_view upAxisName(UpAxis axis);
// Throws std::invalid_argument for a name other than "y" or "z".
[[nodiscard]] UpAxis upAxisFromName(std::string_view name);

struct MeshImportOptions {
    VoxelizeOptions voxelize;
    UpAxis upAxis = UpAxis::Y;
    // The voxel file to write. Empty: "<model name>.voxels" in `directory`.
    std::filesystem::path output;
    // Where the voxel file goes when `output` is empty. Empty: the folder of the mesh.
    std::filesystem::path directory;
};

// The steps of importMesh(), in order, for a progress display.
enum class ImportStep {
    ReadMesh,
    Voxelize,
    WriteFile,
};

struct MeshImportReport {
    std::string meshName;
    std::uint64_t vertices = 0;
    std::uint64_t triangles = 0;
    double readSeconds = 0.0;
    std::string modelName;
    VoxelGrid grid;
    VoxelizeReport voxelize;
    double writeSeconds = 0.0;
    std::filesystem::path output; // the voxel file that was written
    std::uint64_t fileBytes = 0;
};

// The name voxelize() gives the model of a mesh, which is also the name of its voxel file
// without ".voxels". For a mesh file, `meshName` is the file name without extension.
[[nodiscard]] std::string voxelModelName(std::string_view meshName, std::uint32_t resolution);

// The voxel file that importMesh() writes for `meshFile` with `options`.
[[nodiscard]] std::filesystem::path importTarget(const std::filesystem::path& meshFile,
                                                 const MeshImportOptions& options);

// Mesh file to voxel file: reads the mesh file (any kind of mesh::loadMesh), turns +z up into +y
// up if asked, voxelizes it and writes the voxel file (replacing a file of the same name).
// `onStep`, if set, is called at the start of each step, on the calling thread. `monitor`, if
// set, follows the voxelize step; after its `cancel` is set the call throws Cancelled and writes
// no file. Throws on a file that cannot be read or written and on options voxelize() refuses.
MeshImportReport importMesh(const std::filesystem::path& meshFile, const MeshImportOptions& options,
                            const std::function<void(ImportStep)>& onStep = {}, WorkMonitor* monitor = nullptr);

} // namespace thesis::voxel
