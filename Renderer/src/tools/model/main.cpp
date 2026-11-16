// ============================================================================
// READING ORDER: FILE 10
// ============================================================================
// PURPOSE:
// Entry point for the `thesis_model` CLI executable: the offline side of the
// pipeline (`mesh -> voxels -> encoded bricks & JSON reports`).
//
// Subcommands:
//   1. `thesis_model voxelize --mesh lucy.ply --resolution 2048 --output lucy-2048.voxels`
//      Reads a PLY triangle mesh, optionally rotates Z-up meshes (like Stanford Lucy)
//      to Y-up, voxelizes the triangles into N x N x N bricks, and writes `.voxels`.
//   2. `thesis_model report --model lucy-2048.voxels`
//      Encodes the `.voxels` file with every registered encoding (`brick2012_inline`,
//      `brick2012_split`), decodes the GPU buffers back into a `VoxelModel`, verifies
//      that `sameVoxels(decoded, model)` is 100% lossless, prints memory/bits-per-voxel
//      tables, and saves a timestamped JSON report in `results/`.
// ============================================================================

#include "thesis/encodings/BuiltInEncodings.h"
#include "thesis/mesh/TriangleMesh.h"
#include "thesis/metrics/SystemInfo.h"
#include "thesis/voxel/VoxelFile.h"
#include "thesis/voxel/Voxelizer.h"

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <exception>
#include <format>
#include <fstream>
#include <string>
#include <vector>

using namespace thesis;

namespace {

using Clock = std::chrono::steady_clock;

double secondsSince(Clock::time_point start)
{
    return std::chrono::duration<double>(Clock::now() - start).count();
}

std::string fileTimestamp()
{
    const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    return std::format("{:%Y%m%d-%H%M%S}", now);
}

double mebibytes(std::uint64_t bytes)
{
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

unsigned long long count(std::uint64_t value)
{
    return static_cast<unsigned long long>(value);
}

void printModel(const voxel::VoxelModel& model)
{
    std::printf("Model: %s (mesh %s, %llu triangles, rule %s)\n", model.name.c_str(), model.source.mesh.c_str(),
                count(model.source.triangles), model.source.rule.c_str());
    std::printf("Grid:  %u x %u x %u voxels, bricks of %u, voxel size %g\n", model.grid.size.x, model.grid.size.y,
                model.grid.size.z, model.grid.brickSize, static_cast<double>(model.grid.voxelSize));
    std::printf("Data:  %llu filled voxels in %llu bricks\n", count(model.voxelCount()), count(model.bricks.size()));
}

int runVoxelize(const std::filesystem::path& meshPath, std::filesystem::path output,
                const voxel::VoxelizeOptions& options, const std::string& upAxis)
{
    if (upAxis != "y" && upAxis != "z") {
        throw std::invalid_argument("--up-axis must be y or z");
    }
    const Clock::time_point loadStart = Clock::now();
    mesh::TriangleMesh mesh = mesh::loadPly(meshPath);
    if (upAxis == "z") {
        // The renderer treats +y as up. Many scanned statues (like Stanford Lucy) use +z as up.
        // Turn the mesh a quarter turn (-90 degrees) around the X axis: (x, y, z) -> (x, z, -y).
        for (glm::vec3& position : mesh.positions) {
            position = {position.x, position.z, -position.y};
        }
    }
    std::printf("Mesh:  %s, %llu vertices, %llu triangles (read in %.1f s)\n", mesh.name.c_str(),
                count(mesh.positions.size()), count(mesh.triangleCount()), secondsSince(loadStart));

    voxel::VoxelizeReport report;
    const voxel::VoxelModel model = voxel::voxelize(mesh, options, &report);
    printModel(model);
    std::printf("Time:  %.2f s to sort the triangles into bricks, %.2f s to fill the bricks\n", report.binSeconds,
                report.voxelizeSeconds);
    if (report.degenerateTriangles > 0) {
        std::printf("Note:  %llu triangles have no area and were skipped\n", count(report.degenerateTriangles));
    }

    if (output.empty()) {
        output = meshPath.parent_path() / (model.name + ".voxels");
    }
    voxel::saveVoxelModel(output, model);
    std::printf("Wrote: %s (%.1f MiB)\n", output.string().c_str(), mebibytes(std::filesystem::file_size(output)));
    return 0;
}

int runReport(const std::filesystem::path& modelPath, const std::vector<std::string>& requested,
              const std::filesystem::path& outputDirectory)
{
    encoding::EncodingRegistry registry;
    plugins::registerBuiltInEncodings(registry);
    const std::vector<std::string> names = requested.empty() ? registry.names() : requested;
    for (const std::string& name : names) {
        if (!registry.contains(name)) {
            throw std::invalid_argument("Unknown encoding '" + name + "'");
        }
    }

    const Clock::time_point loadStart = Clock::now();
    const voxel::VoxelModel model = voxel::loadVoxelModel(modelPath);
    printModel(model);
    std::printf("       (read in %.1f s)\n\n", secondsSince(loadStart));

    nlohmann::json results;
    results["schema"] = "thesis-model-report/1";
    results["build"] = metrics::buildInfo();
    results["model"] = {{"name", model.name},
                        {"file", modelPath.string()},
                        {"mesh", model.source.mesh},
                        {"triangles", model.source.triangles},
                        {"rule", model.source.rule},
                        {"resolution", model.source.resolution},
                        {"grid_size", {model.grid.size.x, model.grid.size.y, model.grid.size.z}},
                        {"brick_size", model.grid.brickSize},
                        {"bricks", model.bricks.size()},
                        {"voxels", model.voxelCount()}};
    results["encodings"] = nlohmann::json::array();

    bool allRoundTripsOk = true;
    std::printf("%-18s %10s %11s %9s %9s  %s\n", "encoding", "total MiB", "bits/voxel", "encode s", "decode s",
                "decoded == input");
    for (const std::string& name : names) {
        const auto encoder = registry.create(name);

        const Clock::time_point encodeStart = Clock::now();
        const encoding::EncodedModel encoded = encoder->encode(model);
        const double encodeSeconds = secondsSince(encodeStart);

        const Clock::time_point decodeStart = Clock::now();
        const voxel::VoxelModel decoded = encoder->decode(encoded.scene);
        const double decodeSeconds = secondsSince(decodeStart);
        const bool roundTripOk = voxel::sameVoxels(decoded, model);
        allRoundTripsOk = allRoundTripsOk && roundTripOk;

        const encoding::MemoryReport memory = encoding::memoryReport(encoded.scene);
        std::printf("%-18s %10.2f %11.2f %9.2f %9.2f  %s\n", name.c_str(), mebibytes(memory.totalBytes),
                    memory.bitsPerVoxel(model.voxelCount()), encodeSeconds, decodeSeconds, roundTripOk ? "yes" : "NO");
        for (const encoding::MemoryReport::Buffer& buffer : memory.buffers) {
            std::printf("    %-14s %10.2f\n", buffer.name.c_str(), mebibytes(buffer.bytes));
        }

        results["encodings"].push_back({{"name", name},
                                        {"encode_seconds", encodeSeconds},
                                        {"decode_seconds", decodeSeconds},
                                        {"round_trip_ok", roundTripOk},
                                        {"statistics", encoded.statistics},
                                        {"analysis", encoder->analyze(model)}});
    }

    // The cell counts are the same for every layout of one format; print them once.
    const nlohmann::json& first = results["encodings"].front();
    if (first["statistics"].contains("cells")) {
        const nlohmann::json& cells = first["statistics"]["cells"];
        const double total = cells["total"].get<double>();
        std::printf("\nCells of the primary grids (%s):\n", first["name"].get<std::string>().c_str());
        for (const char* kind : {"empty", "height", "pointer"}) {
            const auto cellCount = cells[kind].get<std::uint64_t>();
            std::printf("    %-8s %12llu  %5.1f %%\n", kind, count(cellCount),
                        total > 0.0 ? 100.0 * static_cast<double>(cellCount) / total : 0.0);
        }
    }
    if (!first["analysis"].is_null()) {
        std::printf("\nAnalysis (%s):\n%s\n", first["name"].get<std::string>().c_str(),
                    first["analysis"].dump(2).c_str());
    }

    const std::filesystem::path resultsFile = outputDirectory / (fileTimestamp() + "_model_" + model.name + ".json");
    std::filesystem::create_directories(outputDirectory);
    std::ofstream file(resultsFile);
    if (!file) {
        throw std::runtime_error("Cannot write results file '" + resultsFile.string() + "'");
    }
    file << results.dump(2) << '\n';
    std::printf("\nWrote: %s\n", resultsFile.string().c_str());

    if (!allRoundTripsOk) {
        std::fprintf(stderr, "error: at least one encoding did not return the input voxels\n");
        return 3;
    }
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    CLI::App cli{"Thesis model tool: mesh -> voxel file, and what each encoding makes of a voxel file"};
    cli.require_subcommand(1);

    std::filesystem::path meshPath;
    std::filesystem::path voxelOutput;
    voxel::VoxelizeOptions voxelizeOptions;
    std::string ruleName = std::string(voxel::ruleName(voxelizeOptions.rule));
    CLI::App* voxelize = cli.add_subcommand("voxelize", "Read a triangle mesh (PLY) and write a voxel file");
    voxelize->add_option("--mesh", meshPath, "The PLY file")->required()->check(CLI::ExistingFile);
    voxelize->add_option("--resolution", voxelizeOptions.resolution,
                         "Voxels along the longest side of the mesh (a multiple of the brick size)");
    voxelize->add_option("--brick-size", voxelizeOptions.brickSize, "Voxels along one side of a brick: 8, 16 or 32");
    voxelize->add_option("--rule", ruleName, "Which voxels count as filled: sampled (the paper) or conservative");
    voxelize->add_option("--output", voxelOutput, "The voxel file to write (default: next to the mesh)");
    std::string upAxis = "y";
    voxelize->add_option("--up-axis", upAxis, "The axis of the mesh that points up: y (default) or z, as in Lucy");

    std::filesystem::path modelPath;
    std::vector<std::string> encodings;
    std::filesystem::path reportDirectory = "results";
    CLI::App* report = cli.add_subcommand("report", "Encode a voxel file with every encoding and report the result");
    report->add_option("--model", modelPath, "The voxel file")->required()->check(CLI::ExistingFile);
    report->add_option("--encoding", encodings, "Encoding(s) to report; default: all");
    report->add_option("--output", reportDirectory, "Directory for the results file");

    CLI11_PARSE(cli, argc, argv);

    try {
        if (*voxelize) {
            voxelizeOptions.rule = voxel::ruleFromName(ruleName);
            return runVoxelize(meshPath, voxelOutput, voxelizeOptions, upAxis);
        }
        return runReport(modelPath, encodings, reportDirectory);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "error: %s\n", error.what());
        return 1;
    }
}
