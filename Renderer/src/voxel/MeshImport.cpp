#include "thesis/voxel/MeshImport.h"

#include "thesis/mesh/TriangleMesh.h"
#include "thesis/voxel/VoxelFile.h"

#include <chrono>
#include <stdexcept>

namespace thesis::voxel {
namespace {

using Clock = std::chrono::steady_clock;

double secondsSince(Clock::time_point start)
{
    return std::chrono::duration<double>(Clock::now() - start).count();
}

} // namespace

std::string_view upAxisName(UpAxis axis)
{
    return axis == UpAxis::Z ? "z" : "y";
}

UpAxis upAxisFromName(std::string_view name)
{
    if (name == "y")
        return UpAxis::Y;
    if (name == "z")
        return UpAxis::Z;
    throw std::invalid_argument("Unknown up axis '" + std::string(name) + "' (y or z)");
}

std::string voxelModelName(std::string_view meshName, std::uint32_t resolution)
{
    return std::string(meshName) + "-" + std::to_string(resolution);
}

std::filesystem::path importTarget(const std::filesystem::path& meshFile, const MeshImportOptions& options)
{
    if (!options.output.empty())
        return options.output;
    const std::filesystem::path directory = options.directory.empty() ? meshFile.parent_path() : options.directory;
    return directory / (voxelModelName(meshFile.stem().string(), options.voxelize.resolution) + ".voxels");
}

MeshImportReport importMesh(const std::filesystem::path& meshFile, const MeshImportOptions& options,
                            const std::function<void(ImportStep)>& onStep, WorkMonitor* monitor)
{
    const auto step = [&](ImportStep next) {
        if (monitor != nullptr && monitor->cancel.load())
            throw Cancelled();
        if (onStep)
            onStep(next);
    };
    MeshImportReport report;

    step(ImportStep::ReadMesh);
    const Clock::time_point readStart = Clock::now();
    mesh::TriangleMesh mesh = mesh::loadMesh(meshFile);
    if (options.upAxis == UpAxis::Z) {
        // A quarter turn around x: (x, y, z) -> (x, z, -y), so that +z becomes +y.
        for (glm::vec3& position : mesh.positions) {
            position = {position.x, position.z, -position.y};
        }
    }
    report.readSeconds = secondsSince(readStart);
    report.meshName = mesh.name;
    report.vertices = mesh.positions.size();
    report.triangles = mesh.triangleCount();

    step(ImportStep::Voxelize);
    const VoxelModel model = voxelize(mesh, options.voxelize, &report.voxelize, monitor);
    report.modelName = model.name;
    report.grid = model.grid;

    step(ImportStep::WriteFile);
    const Clock::time_point writeStart = Clock::now();
    report.output = importTarget(meshFile, options);
    if (report.output.has_parent_path())
        std::filesystem::create_directories(report.output.parent_path());
    saveVoxelModel(report.output, model);
    report.writeSeconds = secondsSince(writeStart);
    report.fileBytes = std::filesystem::file_size(report.output);
    return report;
}

} // namespace thesis::voxel
