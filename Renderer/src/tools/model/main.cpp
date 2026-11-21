// thesis_model: the offline side of the pipeline (mesh -> voxels -> encoded bricks).
//
//   thesis_model voxelize --mesh lucy.ply --resolution 2048 --output lucy-2048.voxels
//       Reads a triangle mesh and writes a voxel file.
//   thesis_model report --model lucy-2048.voxels
//       Encodes the voxel file with every encoding, decodes it again and compares the result
//       with the input voxel by voxel. Prints the size of every buffer and writes all numbers
//       to a results file that names the code version.

#include "thesis/encodings/BuiltInEncodings.h"
#include "thesis/metrics/SystemInfo.h"
#include "thesis/voxel/MeshImport.h"
#include "thesis/voxel/VoxelFile.h"

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

int runVoxelize(const std::filesystem::path& meshPath, const voxel::MeshImportOptions& options)
{
    const voxel::MeshImportReport report = voxel::importMesh(meshPath, options);
    const voxel::VoxelizeReport& voxelized = report.voxelize;
    std::printf("Mesh:  %s, %llu vertices, %llu triangles (read in %.1f s)\n", report.meshName.c_str(),
                count(report.vertices), count(report.triangles), report.readSeconds);
    std::printf("Model: %s (rule %s)\n", report.modelName.c_str(),
                std::string(voxel::ruleName(options.voxelize.rule)).c_str());
    std::printf("Grid:  %u x %u x %u voxels, bricks of %u, voxel size %g\n", report.grid.size.x, report.grid.size.y,
                report.grid.size.z, report.grid.brickSize, static_cast<double>(report.grid.voxelSize));
    std::printf("Data:  %llu filled voxels in %llu bricks\n", count(voxelized.voxels), count(voxelized.bricks));
    std::printf("Time:  %.2f s to sort the triangles into bricks, %.2f s to fill the bricks\n", voxelized.binSeconds,
                voxelized.voxelizeSeconds);
    if (voxelized.degenerateTriangles > 0) {
        std::printf("Note:  %llu triangles have no area and were skipped\n", count(voxelized.degenerateTriangles));
    }
    std::printf("Wrote: %s (%.1f MiB)\n", report.output.string().c_str(), mebibytes(report.fileBytes));
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
    voxel::MeshImportOptions importOptions;
    voxel::VoxelizeOptions& voxelizeOptions = importOptions.voxelize;
    std::string ruleName = std::string(voxel::ruleName(voxelizeOptions.rule));
    CLI::App* voxelize = cli.add_subcommand("voxelize", "Read a triangle mesh (PLY, OBJ, glTF) and write a voxel file");
    voxelize->add_option("--mesh", meshPath, "The mesh file: .ply, .obj, .gltf or .glb")
        ->required()
        ->check(CLI::ExistingFile);
    voxelize->add_option("--resolution", voxelizeOptions.resolution,
                         "Voxels along the longest side of the mesh (a multiple of the brick size)");
    voxelize->add_option("--brick-size", voxelizeOptions.brickSize, "Voxels along one side of a brick: 8, 16 or 32");
    voxelize->add_option("--rule", ruleName, "Which voxels count as filled: sampled (the paper) or conservative");
    voxelize->add_option("--output", importOptions.output, "The voxel file to write (default: next to the mesh)");
    std::string upAxis = std::string(voxel::upAxisName(importOptions.upAxis));
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
            importOptions.upAxis = voxel::upAxisFromName(upAxis);
            return runVoxelize(meshPath, importOptions);
        }
        return runReport(modelPath, encodings, reportDirectory);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "error: %s\n", error.what());
        return 1;
    }
}
