// Tests for the offline side: mesh reader, voxelizer, voxel file. They need no GPU.

#include "VoxelTestHelpers.h"

#include "thesis/mesh/TriangleMesh.h"
#include "thesis/voxel/MeshImport.h"
#include "thesis/voxel/OctNormal.h"
#include "thesis/voxel/Rebrick.h"
#include "thesis/voxel/VoxelFile.h"
#include "thesis/voxel/Voxelizer.h"

#include <catch2/catch_test_macros.hpp>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <queue>
#include <string>
#include <tuple>
#include <vector>

using namespace thesis;

namespace {

std::filesystem::path testFile(const char* name)
{
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "thesis_tests";
    std::filesystem::create_directories(directory);
    return directory / name;
}

// A tetrahedron: 4 vertices, 4 triangles.
const std::vector<glm::vec3> kPositions{{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 2.0f, 0.0f}, {0.0f, 0.0f, 3.5f}};
const std::vector<std::uint32_t> kIndices{0, 2, 1, 0, 1, 3, 0, 3, 2, 1, 2, 3};

template <typename T>
void writeBigEndian(std::ofstream& file, T value)
{
    char bytes[sizeof(T)];
    std::memcpy(bytes, &value, sizeof(T));
    std::reverse(std::begin(bytes), std::end(bytes));
    file.write(bytes, sizeof(T));
}

voxel::VoxelModel voxelizeSphere(std::uint32_t resolution, std::uint32_t brickSize, voxel::VoxelizationRule rule)
{
    const mesh::TriangleMesh sphere = mesh::makeSphere({1.0f, -2.0f, 0.5f}, 3.0f, 64);
    return voxel::voxelize(sphere, {resolution, brickSize, rule});
}

} // namespace

TEST_CASE("PLY reader reads text and big-endian binary files", "[mesh]")
{
    const std::filesystem::path textPath = testFile("tetrahedron_text.ply");
    {
        std::ofstream file(textPath);
        file << "ply\nformat ascii 1.0\nelement vertex 4\nproperty float x\nproperty float y\nproperty float z\n"
             << "element face 4\nproperty list uchar int vertex_indices\nend_header\n";
        for (const glm::vec3& p : kPositions) {
            file << p.x << ' ' << p.y << ' ' << p.z << '\n';
        }
        for (std::size_t i = 0; i < kIndices.size(); i += 3) {
            file << "3 " << kIndices[i] << ' ' << kIndices[i + 1] << ' ' << kIndices[i + 2] << '\n';
        }
    }
    const std::filesystem::path binaryPath = testFile("tetrahedron_big_endian.ply");
    {
        std::ofstream file(binaryPath, std::ios::binary);
        file << "ply\nformat binary_big_endian 1.0\nelement vertex 4\nproperty float x\nproperty float y\n"
             << "property float z\nelement face 4\nproperty list uchar int vertex_indices\nend_header\n";
        for (const glm::vec3& p : kPositions) {
            writeBigEndian(file, p.x);
            writeBigEndian(file, p.y);
            writeBigEndian(file, p.z);
        }
        for (std::size_t i = 0; i < kIndices.size(); i += 3) {
            writeBigEndian(file, static_cast<std::uint8_t>(3));
            writeBigEndian(file, static_cast<std::int32_t>(kIndices[i]));
            writeBigEndian(file, static_cast<std::int32_t>(kIndices[i + 1]));
            writeBigEndian(file, static_cast<std::int32_t>(kIndices[i + 2]));
        }
    }

    for (const std::filesystem::path& path : {textPath, binaryPath}) {
        INFO(path.string());
        const mesh::TriangleMesh mesh = mesh::loadPly(path);
        CHECK(mesh.positions == kPositions);
        CHECK(mesh.indices == kIndices);
    }
    CHECK_THROWS(mesh::loadPly(testFile("does_not_exist.ply")));
}

TEST_CASE("packed normals stay within about one degree", "[voxel]")
{
    float worstDegrees = 0.0f;
    for (int i = 0; i < 2000; ++i) {
        // Points spread over the whole sphere (a spiral from pole to pole).
        const float z = 1.0f - 2.0f * (static_cast<float>(i) + 0.5f) / 2000.0f;
        const float radius = std::sqrt(1.0f - z * z);
        const float angle = 2.399963f * static_cast<float>(i);
        const glm::vec3 normal(radius * std::cos(angle), radius * std::sin(angle), z);
        const glm::vec3 decoded = voxel::decodeNormal(voxel::encodeNormal(normal));
        worstDegrees = std::max(worstDegrees, glm::degrees(std::acos(std::min(glm::dot(normal, decoded), 1.0f))));
    }
    CHECK(worstDegrees < 1.5f);

    for (const glm::vec3& axis : {glm::vec3(1, 0, 0), glm::vec3(0, -1, 0), glm::vec3(0, 0, 1), glm::vec3(0, 0, -1)}) {
        CHECK(glm::dot(voxel::decodeNormal(voxel::encodeNormal(axis)), axis) > 0.999f);
    }
}

// A flat square, seen along z: every column that crosses it gets exactly one voxel, all at the
// same height. Worked out by hand: with 32 voxels along the longest side and half a voxel of
// margin, the square spans voxel coordinates 0.5 to 31.5. The centres of columns 1..30 lie
// clearly inside it; columns 0 and 31 lie on its border, where rounding decides.
TEST_CASE("sampled voxelization of a flat square is one layer", "[voxel]")
{
    mesh::TriangleMesh square;
    square.name = "square";
    square.positions = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
    square.indices = {0, 1, 2, 0, 2, 3};

    voxel::VoxelizeReport report;
    const voxel::VoxelModel model = voxel::voxelize(square, {32, 8, voxel::VoxelizationRule::Sampled}, &report);
    model.validate();
    const std::vector<tests::GridVoxel> voxels = tests::listVoxels(model);
    CHECK(report.voxels == voxels.size());
    CHECK(model.grid.size == glm::uvec3(32, 32, 8));

    std::vector<int> perColumn(32 * 32, 0);
    for (const tests::GridVoxel& voxel : voxels) {
        CHECK(voxel.position.z == 0);
        CHECK(voxel::decodeNormal(voxel.normal).z > 0.999f);
        ++perColumn[voxel.position.x + 32 * voxel.position.y];
    }
    for (std::uint32_t y = 0; y < 32; ++y) {
        for (std::uint32_t x = 0; x < 32; ++x) {
            const bool inner = x >= 1 && x <= 30 && y >= 1 && y <= 30;
            INFO("column " << x << ", " << y);
            CHECK(perColumn[x + 32 * y] <= 1);
            if (inner) {
                CHECK(perColumn[x + 32 * y] == 1);
            }
        }
    }
}

// The main property of the sampled rule: no ray can pass through the surface. A flood fill
// through the empty voxels, starting outside, must not reach the inside of a closed surface.
TEST_CASE("sampled voxelization of a sphere has no holes", "[voxel]")
{
    const voxel::VoxelModel model = voxelizeSphere(64, 8, voxel::VoxelizationRule::Sampled);
    model.validate();
    const glm::uvec3 size = model.grid.size;
    const auto cell = [&](const glm::uvec3& p) {
        return p.x + size.x * (p.y + static_cast<std::size_t>(size.y) * p.z);
    };

    std::vector<std::uint8_t> state(static_cast<std::size_t>(size.x) * size.y * size.z, 0); // 1 filled, 2 reached
    const std::vector<tests::GridVoxel> voxels = tests::listVoxels(model);
    REQUIRE(voxels.size() > 5000);
    for (const tests::GridVoxel& voxel : voxels) {
        state[cell(voxel.position)] = 1;

        // Every filled voxel lies on the sphere: its centre is at most one voxel from the surface.
        const glm::vec3 centre = model.grid.origin + model.grid.voxelSize * (glm::vec3(voxel.position) + 0.5f);
        const float distance = glm::length(centre - glm::vec3(1.0f, -2.0f, 0.5f));
        CHECK(std::abs(distance - 3.0f) <= model.grid.voxelSize);
    }

    std::queue<glm::uvec3> open;
    REQUIRE(state[0] == 0);
    state[0] = 2;
    open.push({0, 0, 0});
    while (!open.empty()) {
        const glm::uvec3 p = open.front();
        open.pop();
        for (int axis = 0; axis < 3; ++axis) {
            for (const int step : {-1, 1}) {
                glm::ivec3 next(p);
                next[axis] += step;
                if (next[axis] < 0 || next[axis] >= static_cast<int>(size[axis])) {
                    continue;
                }
                if (state[cell(glm::uvec3(next))] == 0) {
                    state[cell(glm::uvec3(next))] = 2;
                    open.push(glm::uvec3(next));
                }
            }
        }
    }
    CHECK(state[cell(size / 2u)] == 0); // the centre of the sphere: empty, and not reached
}

TEST_CASE("the voxels do not depend on the brick size", "[voxel]")
{
    for (const voxel::VoxelizationRule rule :
         {voxel::VoxelizationRule::Sampled, voxel::VoxelizationRule::Conservative}) {
        INFO(voxel::ruleName(rule));
        const std::vector<tests::GridVoxel> with8 = tests::listVoxels(voxelizeSphere(64, 8, rule));
        const std::vector<tests::GridVoxel> with16 = tests::listVoxels(voxelizeSphere(64, 16, rule));
        const std::vector<tests::GridVoxel> with32 = tests::listVoxels(voxelizeSphere(64, 32, rule));
        CHECK(with8 == with16);
        CHECK(with8 == with32);
    }
}

// One voxel file gives the model with every brick size: rebrick() regroups the voxels. The
// voxels and their normals must stay the same, and the result must keep the rules of a model.
TEST_CASE("regrouping into another brick size keeps every voxel and its normal", "[voxel]")
{
    // A sphere, and noise in a grid that is not a multiple of 32: 24 x 16 x 40 voxels.
    std::vector<voxel::VoxelModel> models;
    for (const std::uint32_t brickSize : {8u, 16u, 32u}) {
        models.push_back(voxelizeSphere(64, brickSize, voxel::VoxelizationRule::Conservative));
    }
    models.push_back(tests::makeModel(8, {3, 2, 5}, [](const glm::uvec3& p) -> std::optional<std::uint16_t> {
        const std::uint32_t h = (p.x * 73856093u) ^ (p.y * 19349663u) ^ (p.z * 83492791u);
        return h % 5 == 0 ? std::optional<std::uint16_t>(static_cast<std::uint16_t>(h >> 8)) : std::nullopt;
    }));

    for (const voxel::VoxelModel& model : models) {
        const std::vector<tests::GridVoxel> expected = tests::listVoxels(model);
        for (const std::uint32_t brickSize : {8u, 16u, 32u}) {
            INFO("from bricks of " << model.grid.brickSize << " to bricks of " << brickSize);
            const voxel::VoxelModel regrouped = voxel::rebrick(model, brickSize);
            regrouped.validate();
            CHECK(regrouped.grid.brickSize == brickSize);
            CHECK(regrouped.grid.origin == model.grid.origin);
            CHECK(regrouped.grid.voxelSize == model.grid.voxelSize);
            CHECK(regrouped.voxelCount() == model.voxelCount());
            CHECK(tests::listVoxels(regrouped) == expected);

            // Back to the first brick size: the same bricks, bit for bit.
            const voxel::VoxelModel back = voxel::rebrick(regrouped, model.grid.brickSize);
            back.validate();
            CHECK(back.bricks == model.bricks);
        }
    }
    CHECK_THROWS(voxel::rebrick(models.front(), 12));
}

TEST_CASE("conservative voxelization contains the sampled one", "[voxel]")
{
    const std::vector<tests::GridVoxel> sampled =
        tests::listVoxels(voxelizeSphere(64, 16, voxel::VoxelizationRule::Sampled));
    const std::vector<tests::GridVoxel> conservative =
        tests::listVoxels(voxelizeSphere(64, 16, voxel::VoxelizationRule::Conservative));
    CHECK(conservative.size() > sampled.size());

    const auto before = [](const tests::GridVoxel& a, const tests::GridVoxel& b) {
        return std::tie(a.position.z, a.position.y, a.position.x) < std::tie(b.position.z, b.position.y, b.position.x);
    };
    std::size_t missing = 0;
    for (const tests::GridVoxel& voxel : sampled) {
        missing += std::binary_search(conservative.begin(), conservative.end(), voxel, before) ? 0u : 1u;
    }
    CHECK(missing == 0);
}

TEST_CASE("voxelizer rejects bad input", "[voxel]")
{
    const mesh::TriangleMesh sphere = mesh::makeSphere({0.0f, 0.0f, 0.0f}, 1.0f, 8);
    CHECK_THROWS(voxel::voxelize(sphere, {64, 12, voxel::VoxelizationRule::Sampled})); // brick size
    CHECK_THROWS(voxel::voxelize(sphere, {70, 32, voxel::VoxelizationRule::Sampled})); // not a multiple
    CHECK_THROWS(voxel::voxelize(mesh::TriangleMesh{}, {64, 32, voxel::VoxelizationRule::Sampled}));
    CHECK_THROWS(voxel::ruleFromName("thick"));
    CHECK(voxel::ruleFromName("conservative") == voxel::VoxelizationRule::Conservative);
}

TEST_CASE("voxel file returns the model that was saved", "[voxel]")
{
    const voxel::VoxelModel model = voxelizeSphere(64, 16, voxel::VoxelizationRule::Sampled);
    const std::filesystem::path path = testFile("sphere.voxels");
    voxel::saveVoxelModel(path, model);
    const voxel::VoxelModel loaded = voxel::loadVoxelModel(path);
    CHECK(loaded == model); // grid, source, name and every voxel

    // The header alone states the same as the whole file.
    const voxel::VoxelFileSummary summary = voxel::readVoxelFileSummary(path);
    CHECK(summary.name == model.name);
    CHECK(summary.grid == model.grid);
    CHECK(summary.source.mesh == model.source.mesh);
    CHECK(summary.source.rule == model.source.rule);
    CHECK(summary.source.resolution == model.source.resolution);
    CHECK(summary.bricks == model.bricks.size());
    CHECK(summary.voxels == model.voxelCount());
    CHECK(summary.fileBytes == std::filesystem::file_size(path));

    // A file that is cut off, and a file of another kind, are refused.
    const std::uintmax_t size = std::filesystem::file_size(path);
    std::filesystem::resize_file(path, size - 100);
    CHECK_THROWS(voxel::loadVoxelModel(path));
    {
        std::ofstream file(path, std::ios::binary);
        file << "this is not a voxel file";
    }
    CHECK_THROWS(voxel::loadVoxelModel(path));
    CHECK_THROWS(voxel::readVoxelFileSummary(path));
}

// OBJ and glTF files of the same mesh give the triangles that the PLY file gives: same corners
// in the same order, positions within the rounding of the text that holds them.
TEST_CASE("OBJ and glTF readers give the mesh of the PLY reader", "[mesh]")
{
    const mesh::TriangleMesh sphere = mesh::makeSphere({1.0f, -2.0f, 0.5f}, 3.0f, 16);
    const std::filesystem::path plyPath = testFile("formats_sphere.ply");
    mesh::savePly(plyPath, sphere);

    // OBJ: one quad (split into two triangles) and the sphere, in two groups.
    const std::filesystem::path objPath = testFile("formats_sphere.obj");
    {
        std::ofstream file(objPath);
        file << std::setprecision(9) << "# test file\no sphere\n";
        for (const glm::vec3& p : sphere.positions)
            file << "v " << p.x << ' ' << p.y << ' ' << p.z << '\n';
        file << "g triangles\n";
        for (std::size_t i = 0; i < sphere.indices.size(); i += 3) // OBJ counts vertices from 1
            file << "f " << sphere.indices[i] + 1 << ' ' << sphere.indices[i + 1] + 1 << ' '
                 << sphere.indices[i + 2] + 1 << '\n';
        // A unit square after the sphere's vertices; OBJ allows negative (relative) indices.
        file << "g quad\nv 10 0 0\nv 11 0 0\nv 11 1 0\nv 10 1 0\nf -4 -3 -2 -1\n";
    }

    // glTF: the sphere under two nodes, a parent that scales by 2 and a child that moves by
    // (1, 2, 3). Its world position is 2 * (p + (1, 2, 3)).
    const std::filesystem::path gltfPath = testFile("formats_sphere.gltf");
    {
        const std::filesystem::path binPath = testFile("formats_sphere.bin");
        std::ofstream bin(binPath, std::ios::binary);
        bin.write(reinterpret_cast<const char*>(sphere.positions.data()),
                  static_cast<std::streamsize>(sphere.positions.size() * sizeof(glm::vec3)));
        bin.write(reinterpret_cast<const char*>(sphere.indices.data()),
                  static_cast<std::streamsize>(sphere.indices.size() * sizeof(std::uint32_t)));
        glm::vec3 lo = sphere.positions.front();
        glm::vec3 hi = lo;
        for (const glm::vec3& p : sphere.positions) {
            lo = glm::min(lo, p);
            hi = glm::max(hi, p);
        }
        const std::size_t positionBytes = sphere.positions.size() * sizeof(glm::vec3);
        const std::size_t indexBytes = sphere.indices.size() * sizeof(std::uint32_t);
        std::ofstream file(gltfPath);
        file << std::setprecision(9) << R"({"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}],)"
             << R"("nodes": [{"children": [1], "scale": [2, 2, 2]}, {"mesh": 0, "translation": [1, 2, 3]}],)"
             << R"("meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "indices": 1}]}],)"
             << R"("accessors": [{"bufferView": 0, "componentType": 5126, "type": "VEC3", "count": )"
             << sphere.positions.size() << R"(, "min": [)" << lo.x << ',' << lo.y << ',' << lo.z << R"(], "max": [)"
             << hi.x << ',' << hi.y << ',' << hi.z << "]},"
             << R"({"bufferView": 1, "componentType": 5125, "type": "SCALAR", "count": )" << sphere.indices.size()
             << "}],"
             << R"("bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": )" << positionBytes << "},"
             << R"({"buffer": 0, "byteOffset": )" << positionBytes << R"(, "byteLength": )" << indexBytes << "}],"
             << R"("buffers": [{"uri": "formats_sphere.bin", "byteLength": )" << positionBytes + indexBytes << "}]}";
    }

    const auto checkPositions = [](const std::vector<glm::vec3>& actual, const std::vector<glm::vec3>& expected) {
        REQUIRE(actual.size() == expected.size());
        for (std::size_t i = 0; i < actual.size(); ++i) {
            INFO("vertex " << i);
            CHECK(glm::length(actual[i] - expected[i]) <= 1e-5f * (1.0f + glm::length(expected[i])));
        }
    };

    const mesh::TriangleMesh ply = mesh::loadMesh(plyPath);
    CHECK(ply.indices == sphere.indices);
    checkPositions(ply.positions, sphere.positions);

    const mesh::TriangleMesh obj = mesh::loadMesh(objPath);
    CHECK(obj.name == "formats_sphere");
    // The sphere first, then the square as two triangles of its four corners.
    REQUIRE(obj.triangleCount() == sphere.triangleCount() + 2);
    CHECK(std::equal(sphere.indices.begin(), sphere.indices.end(), obj.indices.begin()));
    const auto square = static_cast<std::uint32_t>(sphere.positions.size());
    for (std::size_t i = sphere.indices.size(); i < obj.indices.size(); ++i)
        CHECK((obj.indices[i] >= square && obj.indices[i] < square + 4));
    std::vector<glm::vec3> objPositions = sphere.positions;
    objPositions.insert(objPositions.end(), {{10, 0, 0}, {11, 0, 0}, {11, 1, 0}, {10, 1, 0}});
    checkPositions(obj.positions, objPositions);

    const mesh::TriangleMesh gltf = mesh::loadMesh(gltfPath);
    CHECK(gltf.indices == sphere.indices);
    std::vector<glm::vec3> placed = sphere.positions;
    for (glm::vec3& p : placed)
        p = 2.0f * (p + glm::vec3(1.0f, 2.0f, 3.0f));
    checkPositions(gltf.positions, placed);

    CHECK(mesh::isMeshFile("a/b/Model.GLB"));
    CHECK_FALSE(mesh::isMeshFile("model.stl"));
    CHECK_THROWS(mesh::loadMesh(testFile("model.stl")));
    CHECK_THROWS(mesh::loadMesh(testFile("does_not_exist.obj")));
    CHECK_THROWS(mesh::loadMesh(testFile("does_not_exist.gltf")));
}

// "Import mesh" in the window shows the share done and can stop the import.
TEST_CASE("voxelizer reports its progress and stops when cancelled", "[voxel]")
{
    const mesh::TriangleMesh sphere = mesh::makeSphere({0.0f, 0.0f, 0.0f}, 1.0f, 32);
    const voxel::VoxelizeOptions options{64, 16, voxel::VoxelizationRule::Sampled};

    voxel::WorkMonitor monitor;
    const voxel::VoxelModel model = voxel::voxelize(sphere, options, nullptr, &monitor);
    CHECK(monitor.done.load() == 1.0f);
    CHECK(model == voxel::voxelize(sphere, options)); // following the work does not change it

    voxel::WorkMonitor cancelled;
    cancelled.cancel = true;
    CHECK_THROWS_AS(voxel::voxelize(sphere, options, nullptr, &cancelled), voxel::Cancelled);

    // A cancelled import writes no file.
    const std::filesystem::path meshFile = testFile("cancelled_sphere.ply");
    mesh::savePly(meshFile, sphere);
    voxel::MeshImportOptions importOptions;
    importOptions.voxelize = options;
    importOptions.output = testFile("cancelled_sphere.voxels");
    std::filesystem::remove(importOptions.output);
    CHECK_THROWS_AS(voxel::importMesh(meshFile, importOptions, {}, &cancelled), voxel::Cancelled);
    CHECK_FALSE(std::filesystem::exists(importOptions.output));
}

TEST_CASE("PLY writer gives back the mesh it was given", "[mesh]")
{
    mesh::TriangleMesh tetrahedron;
    tetrahedron.positions = kPositions;
    tetrahedron.indices = kIndices;
    const std::filesystem::path path = testFile("tetrahedron_written.ply");
    mesh::savePly(path, tetrahedron);
    const mesh::TriangleMesh loaded = mesh::loadPly(path);
    CHECK(loaded.name == "tetrahedron_written");
    CHECK(loaded.positions == kPositions);
    CHECK(loaded.indices == kIndices);
}

// importMesh() is the path of thesis_model voxelize and of "Import mesh" in the window. Its file
// must hold exactly what voxelize() makes of the turned mesh.
TEST_CASE("mesh import writes the voxel file of the turned mesh", "[voxel]")
{
    // A sphere moved off the origin, so that the quarter turn changes the grid origin.
    mesh::TriangleMesh sphere = mesh::makeSphere({1.0f, -2.0f, 0.5f}, 3.0f, 32);
    sphere.name = "import_sphere";
    const std::filesystem::path meshFile = testFile("import_sphere.ply");
    mesh::savePly(meshFile, sphere);
    const std::filesystem::path directory = testFile("imported");
    std::filesystem::remove_all(directory);

    for (const voxel::UpAxis axis : {voxel::UpAxis::Y, voxel::UpAxis::Z}) {
        INFO("up axis " << voxel::upAxisName(axis));
        voxel::MeshImportOptions options;
        options.voxelize = {64, 16, voxel::VoxelizationRule::Conservative};
        options.upAxis = axis;
        options.directory = directory;
        std::vector<voxel::ImportStep> steps;
        const voxel::MeshImportReport report =
            voxel::importMesh(meshFile, options, [&](voxel::ImportStep step) { steps.push_back(step); });

        CHECK(steps ==
              std::vector{voxel::ImportStep::ReadMesh, voxel::ImportStep::Voxelize, voxel::ImportStep::WriteFile});
        CHECK(report.output == directory / "import_sphere-64.voxels");
        CHECK(report.output == voxel::importTarget(meshFile, options));
        CHECK(report.triangles == sphere.triangleCount());
        CHECK(report.fileBytes == std::filesystem::file_size(report.output));

        mesh::TriangleMesh turned = sphere;
        if (axis == voxel::UpAxis::Z) {
            for (glm::vec3& p : turned.positions)
                p = {p.x, p.z, -p.y};
        }
        const voxel::VoxelModel expected = voxel::voxelize(turned, options.voxelize);
        const voxel::VoxelModel loaded = voxel::loadVoxelModel(report.output);
        CHECK(loaded == expected);
        CHECK(report.modelName == loaded.name);
        CHECK(report.grid == loaded.grid);
        CHECK(report.voxelize.voxels == loaded.voxelCount());
    }

    // The file of the import replaced the first one: one file per mesh and resolution.
    CHECK(std::distance(std::filesystem::directory_iterator(directory), std::filesystem::directory_iterator{}) == 1);

    voxel::MeshImportOptions badResolution;
    badResolution.voxelize.resolution = 70;
    badResolution.directory = directory;
    CHECK_THROWS(voxel::importMesh(meshFile, badResolution));
    CHECK_THROWS(voxel::importMesh(testFile("does_not_exist.ply"), voxel::MeshImportOptions{}));
    CHECK_THROWS(voxel::upAxisFromName("x"));
    CHECK(voxel::upAxisFromName("z") == voxel::UpAxis::Z);
}
