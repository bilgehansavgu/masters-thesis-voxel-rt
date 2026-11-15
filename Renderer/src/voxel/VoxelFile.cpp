#include "thesis/voxel/VoxelFile.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>

namespace thesis::voxel {
namespace {

constexpr std::array<char, 8> kMagic{'T', 'H', 'V', 'O', 'X', 'E', 'L', '1'};
constexpr int kVersion = 1;

template <typename T>
void writeValues(std::ofstream& file, const T* values, std::size_t count)
{
    file.write(reinterpret_cast<const char*>(values), static_cast<std::streamsize>(count * sizeof(T)));
}

template <typename T>
void readValues(std::ifstream& file, T* values, std::size_t count)
{
    file.read(reinterpret_cast<char*>(values), static_cast<std::streamsize>(count * sizeof(T)));
}

} // namespace

void saveVoxelModel(const std::filesystem::path& path, const VoxelModel& model)
{
    model.validate();
    const std::uint64_t bricks = model.bricks.size();
    const std::uint64_t voxels = model.voxelCount();
    const std::uint64_t wordsPerBrick = voxelsPerBrick(model.grid.brickSize) / 64;

    // Sections, in file order. Offsets count from the first byte after the header.
    const std::uint64_t coordinateBytes = bricks * 3 * sizeof(std::uint32_t);
    const std::uint64_t countBytes = bricks * sizeof(std::uint32_t);
    const std::uint64_t occupancyBytes = bricks * wordsPerBrick * sizeof(std::uint64_t);
    const std::uint64_t normalBytes = voxels * sizeof(std::uint16_t);

    nlohmann::json header;
    header["format"] = "thesis-voxels";
    header["version"] = kVersion;
    header["name"] = model.name;
    header["grid"] = {{"origin", {model.grid.origin.x, model.grid.origin.y, model.grid.origin.z}},
                      {"voxel_size", model.grid.voxelSize},
                      {"size", {model.grid.size.x, model.grid.size.y, model.grid.size.z}},
                      {"brick_size", model.grid.brickSize}};
    header["source"] = {{"mesh", model.source.mesh},
                        {"triangles", model.source.triangles},
                        {"rule", model.source.rule},
                        {"resolution", model.source.resolution}};
    header["bricks"] = bricks;
    header["voxels"] = voxels;
    header["sections"] = {
        {"brick_coordinates", {{"offset", 0}, {"bytes", coordinateBytes}}},
        {"brick_voxel_counts", {{"offset", coordinateBytes}, {"bytes", countBytes}}},
        {"occupancy", {{"offset", coordinateBytes + countBytes}, {"bytes", occupancyBytes}}},
        {"normals", {{"offset", coordinateBytes + countBytes + occupancyBytes}, {"bytes", normalBytes}}}};
    const std::string headerText = header.dump(2);
    const std::uint64_t headerBytes = headerText.size();

    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot write voxel file '" + path.string() + "'");
    }
    file.write(kMagic.data(), static_cast<std::streamsize>(kMagic.size()));
    writeValues(file, &headerBytes, 1);
    file.write(headerText.data(), static_cast<std::streamsize>(headerText.size()));

    for (const VoxelBrick& brick : model.bricks) {
        const std::uint32_t coordinate[3] = {brick.coordinate.x, brick.coordinate.y, brick.coordinate.z};
        writeValues(file, coordinate, 3);
    }
    for (const VoxelBrick& brick : model.bricks) {
        const auto count = static_cast<std::uint32_t>(brick.normals.size());
        writeValues(file, &count, 1);
    }
    for (const VoxelBrick& brick : model.bricks) {
        writeValues(file, brick.occupancy.data(), brick.occupancy.size());
    }
    for (const VoxelBrick& brick : model.bricks) {
        writeValues(file, brick.normals.data(), brick.normals.size());
    }
    if (!file) {
        throw std::runtime_error("Writing voxel file '" + path.string() + "' failed (disk full?)");
    }
}

VoxelModel loadVoxelModel(const std::filesystem::path& path)
{
    const auto fail = [&](const std::string& what) {
        throw std::runtime_error("Voxel file '" + path.string() + "': " + what);
    };

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        fail("cannot be opened");
    }
    std::array<char, 8> magic{};
    std::uint64_t headerBytes = 0;
    file.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    readValues(file, &headerBytes, 1);
    if (!file || magic != kMagic || headerBytes > (1ull << 20)) {
        fail("is not a voxel file of this program");
    }
    std::string headerText(static_cast<std::size_t>(headerBytes), '\0');
    file.read(headerText.data(), static_cast<std::streamsize>(headerText.size()));
    if (!file) {
        fail("is cut off inside the header");
    }

    VoxelModel model;
    std::uint64_t bricks = 0;
    std::uint64_t voxels = 0;
    nlohmann::json sections;
    try {
        const nlohmann::json header = nlohmann::json::parse(headerText);
        if (header.at("format").get<std::string>() != "thesis-voxels" || header.at("version").get<int>() != kVersion) {
            fail("has an unknown format or version");
        }
        model.name = header.at("name").get<std::string>();
        const nlohmann::json& grid = header.at("grid");
        const auto origin = grid.at("origin").get<std::array<float, 3>>();
        const auto size = grid.at("size").get<std::array<std::uint32_t, 3>>();
        model.grid.origin = {origin[0], origin[1], origin[2]};
        model.grid.voxelSize = grid.at("voxel_size").get<float>();
        model.grid.size = {size[0], size[1], size[2]};
        model.grid.brickSize = grid.at("brick_size").get<std::uint32_t>();
        const nlohmann::json& source = header.at("source");
        model.source.mesh = source.at("mesh").get<std::string>();
        model.source.triangles = source.at("triangles").get<std::uint64_t>();
        model.source.rule = source.at("rule").get<std::string>();
        model.source.resolution = source.at("resolution").get<std::uint32_t>();
        bricks = header.at("bricks").get<std::uint64_t>();
        voxels = header.at("voxels").get<std::uint64_t>();
        sections = header.at("sections");
    } catch (const nlohmann::json::exception& error) {
        fail(std::string("has a damaged header: ") + error.what());
    }
    if (!isSupportedBrickSize(model.grid.brickSize)) {
        fail("has an unsupported brick size");
    }

    const std::uint64_t dataStart = kMagic.size() + sizeof(std::uint64_t) + headerBytes;
    const std::size_t wordsPerBrick = voxelsPerBrick(model.grid.brickSize) / 64;
    const auto openSection = [&](const char* name, std::uint64_t expectedBytes) {
        const auto offset = sections.at(name).at("offset").get<std::uint64_t>();
        const auto bytes = sections.at(name).at("bytes").get<std::uint64_t>();
        if (bytes != expectedBytes) {
            fail(std::string("section '") + name + "' has the wrong size");
        }
        file.seekg(static_cast<std::streamoff>(dataStart + offset));
    };

    model.bricks.resize(static_cast<std::size_t>(bricks));
    std::vector<std::uint32_t> counts(model.bricks.size());
    try {
        openSection("brick_coordinates", bricks * 3 * sizeof(std::uint32_t));
        for (VoxelBrick& brick : model.bricks) {
            std::uint32_t coordinate[3] = {};
            readValues(file, coordinate, 3);
            brick.coordinate = {coordinate[0], coordinate[1], coordinate[2]};
        }
        openSection("brick_voxel_counts", bricks * sizeof(std::uint32_t));
        readValues(file, counts.data(), counts.size());
        openSection("occupancy", bricks * wordsPerBrick * sizeof(std::uint64_t));
        for (VoxelBrick& brick : model.bricks) {
            brick.occupancy.resize(wordsPerBrick);
            readValues(file, brick.occupancy.data(), wordsPerBrick);
        }
        openSection("normals", voxels * sizeof(std::uint16_t));
        for (std::size_t i = 0; i < model.bricks.size(); ++i) {
            model.bricks[i].normals.resize(counts[i]);
            readValues(file, model.bricks[i].normals.data(), counts[i]);
        }
    } catch (const nlohmann::json::exception& error) {
        fail(std::string("has a damaged section table: ") + error.what());
    }
    if (!file) {
        fail("is cut off inside the data");
    }
    model.validate();
    if (model.voxelCount() != voxels) {
        fail("does not hold the number of voxels that its header states");
    }
    return model;
}

} // namespace thesis::voxel
