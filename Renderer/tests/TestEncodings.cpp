// Tests for the encoding socket. The rule for every encoding: decoding the encoded data gives
// back the input voxels exactly. New encodings are covered automatically. No GPU needed.

#include "VoxelTestHelpers.h"

#include "thesis/brick2012/BrickBuffers.h"
#include "thesis/brick2012/ColumnBrick.h"
#include "thesis/encodings/BuiltInEncodings.h"
#include "thesis/mesh/TriangleMesh.h"
#include "thesis/shared/Bindings.h"
#include "thesis/shared/Brick2012.h"
#include "thesis/voxel/Voxelizer.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace thesis;

namespace {

encoding::EncodingRegistry makeRegistry()
{
    encoding::EncodingRegistry registry;
    plugins::registerBuiltInEncodings(registry);
    return registry;
}

// Deterministic pseudo-random bits (SplitMix64), the same on every platform.
std::uint64_t hash(std::uint64_t value)
{
    value += 0x9E3779B97F4A7C15ull;
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
    return value ^ (value >> 31);
}

std::uint64_t hashOf(const glm::uvec3& p)
{
    return hash(p.x + 4096ull * (p.y + 4096ull * p.z));
}

// Models that together cover the cases an encoding must handle.
struct NamedModel {
    std::string name;
    voxel::VoxelModel model;
};

std::vector<NamedModel> testModels()
{
    std::vector<NamedModel> models;
    const mesh::TriangleMesh sphere = mesh::makeSphere({0.0f, 0.0f, 0.0f}, 1.0f, 48);
    for (const std::uint32_t brickSize : {8u, 16u, 32u}) {
        models.push_back({"sphere, bricks of " + std::to_string(brickSize),
                          voxel::voxelize(sphere, {64, brickSize, voxel::VoxelizationRule::Sampled})});
    }
    models.push_back(
        {"sphere, conservative", voxel::voxelize(sphere, {64, 16, voxel::VoxelizationRule::Conservative})});

    // Noise: about a third of all voxels filled, so most columns have several short runs.
    models.push_back({"noise", tests::makeModel(16, {2, 1, 2}, [](const glm::uvec3& p) -> std::optional<std::uint16_t> {
                          const std::uint64_t h = hashOf(p);
                          return h % 3 == 0 ? std::optional<std::uint16_t>(static_cast<std::uint16_t>(h >> 20))
                                            : std::nullopt;
                      })});

    // One brick with the special columns: empty, one voxel at each end, a full column, a
    // column of single voxels with gaps, and two runs that touch the ends.
    models.push_back(
        {"special columns", tests::makeModel(8, {1, 1, 1}, [](const glm::uvec3& p) -> std::optional<std::uint16_t> {
             const std::uint16_t normal = static_cast<std::uint16_t>(0x7F7F + p.x + 8 * p.y + 64 * p.z);
             const bool filled = (p.x == 1 && p.y == 0 && p.z == 0) || (p.x == 2 && p.y == 0 && p.z == 7) ||
                                 (p.x == 3 && p.y == 0) || (p.x == 4 && p.y == 0 && p.z % 2 == 0) ||
                                 (p.x == 5 && p.y == 0 && (p.z <= 1 || p.z >= 6)) || (p.x == 7 && p.y == 7);
             return filled ? std::optional<std::uint16_t>(normal) : std::nullopt;
         })});

    // A single voxel in the last brick of a larger grid: brick coordinates and offsets.
    models.push_back(
        {"one voxel", tests::makeModel(8, {3, 2, 4}, [](const glm::uvec3& p) -> std::optional<std::uint16_t> {
             return p == glm::uvec3(23, 15, 31) ? std::optional<std::uint16_t>(std::uint16_t{0x1234}) : std::nullopt;
         })});
    return models;
}

} // namespace

TEST_CASE("every encoding returns the input voxels", "[encoding]")
{
    const encoding::EncodingRegistry registry = makeRegistry();
    REQUIRE(registry.names().size() >= 2);
    const std::vector<NamedModel> models = testModels();

    for (const std::string& name : registry.names()) {
        const auto encoder = registry.create(name);
        CHECK(encoder->name() == name);
        for (const NamedModel& input : models) {
            INFO(name << " on " << input.name);
            input.model.validate();
            const encoding::EncodedModel encoded = encoder->encode(input.model);

            // The scene is ready for the GPU: one primitive per brick, named after the encoding.
            CHECK(encoded.scene.primitiveKind == name);
            CHECK(encoded.scene.primitiveBounds.size() == input.model.bricks.size());
            CHECK_FALSE(encoded.scene.bounds.empty());

            const voxel::VoxelModel decoded = encoder->decode(encoded.scene);
            decoded.validate();
            CHECK(decoded.grid == input.model.grid);
            CHECK(tests::listVoxels(decoded) == tests::listVoxels(input.model));
            CHECK(voxel::sameVoxels(decoded, input.model));

            // The same input gives the same bytes: results can be reproduced.
            const encoding::EncodedModel again = encoder->encode(input.model);
            REQUIRE(again.scene.primitiveData.size() == encoded.scene.primitiveData.size());
            for (std::size_t i = 0; i < encoded.scene.primitiveData.size(); ++i) {
                CHECK(again.scene.primitiveData[i].bytes == encoded.scene.primitiveData[i].bytes);
            }

            const encoding::MemoryReport memory = encoding::memoryReport(encoded.scene);
            CHECK(memory.buffers.size() == encoded.scene.primitiveData.size() + 1);
            CHECK(encoded.statistics.at("memory").at("total_bytes").get<std::uint64_t>() == memory.totalBytes);
            CHECK(encoded.statistics.at("voxels").get<std::uint64_t>() == input.model.voxelCount());
        }
    }
}

TEST_CASE("every encoding has settings of its own", "[encoding]")
{
    const encoding::EncodingRegistry registry = makeRegistry();
    const std::vector<std::string> names = registry.names();
    for (std::size_t i = 0; i < names.size(); ++i) {
        INFO(names[i]);
        const auto encoder = registry.create(names[i]);
        const std::vector<encoding::EncodingSetting> settings = encoder->settings();
        // Without settings the window cannot offer the encoding.
        CHECK_FALSE(settings.empty());
        for (std::size_t j = 0; j < i; ++j) {
            INFO("and " << names[j]);
            CHECK(settings != registry.create(names[j])->settings());
        }
        // The twin stores the same bricks with skipping the other way: exactly one setting differs.
        if (!encoder->skippingTwin().empty()) {
            const std::vector<encoding::EncodingSetting> twin = registry.create(encoder->skippingTwin())->settings();
            REQUIRE(twin.size() == settings.size());
            std::size_t differences = 0;
            for (std::size_t k = 0; k < settings.size(); ++k) {
                CHECK(twin[k].name == settings[k].name);
                differences += twin[k].value != settings[k].value ? 1u : 0u;
            }
            CHECK(differences == 1);
        }
    }
}

TEST_CASE("the encoding switches reach every encoding and keep the other settings", "[encoding]")
{
    const encoding::EncodingRegistry registry = makeRegistry();
    const encoding::EncodingChoices choices = encoding::EncodingChoices::of(registry);
    const std::size_t count = registry.names().size();
    for (std::size_t from = 0; from < count; ++from) {
        for (std::size_t to = 0; to < count; ++to) {
            INFO(registry.names()[from] << " -> " << registry.names()[to]);
            // Setting each switch to the value of the target, one after the other, ends at the target.
            std::size_t current = from;
            for (const encoding::EncodingSetting& wanted : choices.settingsOf(to)) {
                const std::size_t next = choices.pick(current, wanted.name, wanted.value);
                CHECK(choices.valueOf(next, wanted.name) == wanted.value);
                current = next;
            }
            CHECK(current == to);
        }
    }
    // Every value of every switch belongs to some encoding.
    for (const encoding::EncodingChoices::Switch& option : choices.switches()) {
        for (const std::string& value : option.values) {
            CHECK_NOTHROW(choices.pick(0, option.setting, value));
        }
    }
    CHECK_THROWS_AS(choices.pick(0, "no such setting", "x"), std::invalid_argument);

    // Two encodings with the same settings are a programming error: the switches could not reach both.
    CHECK_THROWS_AS(encoding::EncodingChoices({{{"A", "1"}}, {{"A", "1"}}}), std::logic_error);
    // Keeping the other settings: from (A 1, B 1), setting A to 2 picks (A 2, B 1), not (A 2, B 2).
    const encoding::EncodingChoices grid(
        {{{"A", "1"}, {"B", "1"}}, {{"A", "2"}, {"B", "2"}}, {{"A", "2"}, {"B", "1"}}});
    CHECK(grid.pick(0, "A", "2") == 2);
    CHECK(grid.describe(2) == "A 2, B 1");
}

TEST_CASE("brick bounds enclose exactly the filled voxels", "[encoding]")
{
    const voxel::VoxelModel model =
        tests::makeModel(8, {2, 1, 1}, [](const glm::uvec3& p) -> std::optional<std::uint16_t> {
            // Brick 0: voxels (1,2,3) and (4,2,6). Brick 1: voxel (8+7, 0, 0).
            const bool filled = p == glm::uvec3(1, 2, 3) || p == glm::uvec3(4, 2, 6) || p == glm::uvec3(15, 0, 0);
            return filled ? std::optional<std::uint16_t>(std::uint16_t{0}) : std::nullopt;
        });
    const encoding::EncodingRegistry registry = makeRegistry();
    for (const std::string& name : registry.names()) {
        INFO(name);
        const core::Scene scene = registry.create(name)->encode(model).scene;
        REQUIRE(scene.primitiveBounds.size() == 2);
        // makeModel: origin (-1, 2, 0.5), voxel size 0.25.
        CHECK(scene.primitiveBounds[0].lo == glm::vec3(-1.0f + 0.25f * 1, 2.0f + 0.25f * 2, 0.5f + 0.25f * 3));
        CHECK(scene.primitiveBounds[0].hi == glm::vec3(-1.0f + 0.25f * 5, 2.0f + 0.25f * 3, 0.5f + 0.25f * 7));
        CHECK(scene.primitiveBounds[1].lo == glm::vec3(-1.0f + 0.25f * 15, 2.0f, 0.5f));
        CHECK(scene.primitiveBounds[1].hi == glm::vec3(-1.0f + 0.25f * 16, 2.0f + 0.25f, 0.5f + 0.25f));
    }
}

// A full 3D checkerboard has the most runs a brick can have: 16 single voxels in each of the
// 1024 columns. The 16-bit pointer cell of the split layout cannot address that much run data,
// so the encoder must say so instead of writing wrong offsets. The inline layout must cope.
TEST_CASE("split layout refuses a brick with too many runs", "[encoding]")
{
    const voxel::VoxelModel checkerboard =
        tests::makeModel(32, {1, 1, 1}, [](const glm::uvec3& p) -> std::optional<std::uint16_t> {
            return (p.x + p.y + p.z) % 2 == 0 ? std::optional<std::uint16_t>(std::uint16_t{0x8080}) : std::nullopt;
        });
    const encoding::EncodingRegistry registry = makeRegistry();
    CHECK_THROWS(registry.create("brick2012_split")->encode(checkerboard));

    const auto inlineEncoder = registry.create("brick2012_inline");
    CHECK(voxel::sameVoxels(inlineEncoder->decode(inlineEncoder->encode(checkerboard).scene), checkerboard));
}

// The mipmap is checked against the columns directly: for every square of every level, the
// lowest and the highest filled voxel of all columns of the square, found by a loop over them.
TEST_CASE("the min/max mipmap holds the lowest and the highest voxel of each square", "[encoding][mipmap]")
{
    const encoding::EncodingRegistry registry = makeRegistry();
    const std::vector<NamedModel> models = testModels();
    const auto bytesAt = [](const core::Scene& scene, std::uint32_t binding) -> const std::vector<std::byte>* {
        for (const core::PrimitiveData& data : scene.primitiveData) {
            if (data.binding == binding) {
                return &data.bytes;
            }
        }
        return nullptr;
    };

    std::size_t encodingsWithMipmap = 0;
    std::size_t expectedWithMipmap = 0;
    for (const std::string& name : registry.names()) {
        const auto encoder = registry.create(name);
        expectedWithMipmap += encoder->skipsEmptySpace() ? models.size() : 0;
        for (const NamedModel& input : models) {
            INFO(name << " on " << input.name);
            const core::Scene scene = encoder->encode(input.model).scene;
            const std::vector<std::byte>* mipmap = bytesAt(scene, THESIS_BINDING_BRICK_MIPMAP);
            brick2012::BrickModelHeader header;
            std::memcpy(&header, bytesAt(scene, THESIS_BINDING_BRICK_HEADER)->data(), sizeof(header));
            const std::uint32_t n = header.brickSize;
            const bool hasMipmap = encoder->skipsEmptySpace();
            if (!hasMipmap) {
                CHECK(mipmap == nullptr);
                CHECK(header.mipmapLevels == 0);
                continue;
            }
            REQUIRE(mipmap != nullptr);
            REQUIRE((1u << (header.mipmapLevels + 1)) == n); // the last level has 2 x 2 cells
            const std::size_t cellsOfBrick = THESIS_MIP_CELLS_OF_BRICK(n);
            REQUIRE(mipmap->size() >= input.model.bricks.size() * cellsOfBrick * sizeof(std::uint16_t));
            const std::vector<std::byte>& table = *bytesAt(scene, THESIS_BINDING_BRICK_TABLE);

            std::size_t wrong = 0;
            std::size_t cellsWithVoxels = 0;
            for (std::size_t b = 0; b < input.model.bricks.size(); ++b) {
                brick2012::BrickRecord record;
                std::memcpy(&record, table.data() + b * sizeof(record), sizeof(record));
                const brick2012::ColumnBrick columns =
                    brick2012::toColumns(input.model.bricks[b], n, record.position >> THESIS_BRICK_AXIS_SHIFT);
                for (std::uint32_t level = 1; level <= header.mipmapLevels; ++level) {
                    const std::uint32_t side = n >> level;
                    for (std::uint32_t y = 0; y < side; ++y) {
                        for (std::uint32_t x = 0; x < side; ++x) {
                            std::uint32_t lowest = 63;
                            std::uint32_t highest = 0;
                            for (std::uint32_t v = y << level; v < (y + 1) << level; ++v) {
                                for (std::uint32_t u = x << level; u < (x + 1) << level; ++u) {
                                    for (const brick2012::Run& run : columns.runs(u + n * v)) {
                                        lowest = std::min<std::uint32_t>(lowest, run.start);
                                        highest = std::max<std::uint32_t>(highest, run.start + run.length - 1u);
                                    }
                                }
                            }
                            std::uint16_t stored;
                            const std::size_t index =
                                b * cellsOfBrick + THESIS_MIP_LEVEL_START(n, level) + x + side * y;
                            std::memcpy(&stored, mipmap->data() + index * sizeof(stored), sizeof(stored));
                            wrong += stored != (lowest | (highest << THESIS_MIP_HIGH_SHIFT)) ? 1 : 0;
                            cellsWithVoxels += lowest <= highest ? 1 : 0;
                        }
                    }
                }
            }
            CHECK(wrong == 0);
            CHECK(cellsWithVoxels > 0);
            ++encodingsWithMipmap;
        }
    }
    CHECK(expectedWithMipmap > 0);
    CHECK(encodingsWithMipmap == expectedWithMipmap);
}

TEST_CASE("columns along each axis give back the brick", "[encoding]")
{
    const voxel::VoxelModel model =
        tests::makeModel(16, {1, 1, 1}, [](const glm::uvec3& p) -> std::optional<std::uint16_t> {
            const std::uint64_t h = hashOf(p);
            return h % 4 == 0 ? std::optional<std::uint16_t>(static_cast<std::uint16_t>(h >> 16)) : std::nullopt;
        });
    REQUIRE(model.bricks.size() == 1);
    const voxel::VoxelBrick& brick = model.bricks.front();

    for (std::uint32_t axis = 0; axis < 3; ++axis) {
        INFO("axis " << axis);
        const brick2012::ColumnBrick columns = brick2012::toColumns(brick, 16, axis);
        CHECK(brick2012::fromColumns(columns, brick.coordinate) == brick);

        brick2012::ColumnStatistics statistics;
        statistics.add(columns);
        CHECK(statistics.voxels == brick.normals.size());
        CHECK(statistics.emptyCells + statistics.heightCells + statistics.pointerCells == 16 * 16);
        CHECK(statistics.bricksByAxis[axis] == 1);
    }
}

TEST_CASE("primary axis follows the normals", "[encoding]")
{
    // A flat layer of voxels at z = 3 whose normals point along z: columns along z hold one voxel each.
    const voxel::VoxelModel layer =
        tests::makeModel(8, {1, 1, 1}, [](const glm::uvec3& p) -> std::optional<std::uint16_t> {
            return p.z == 3 ? std::optional<std::uint16_t>(std::uint16_t{0x7F7F}) : std::nullopt; // packed (0, 0, 1)
        });
    REQUIRE(layer.bricks.size() == 1);
    CHECK(brick2012::choosePrimaryAxis(layer.bricks.front()) == 2);

    brick2012::ColumnStatistics statistics;
    statistics.add(brick2012::toColumns(layer.bricks.front(), 8, 2));
    CHECK(statistics.heightCells == 64);
    CHECK(statistics.pointerCells == 0);

    const brick2012::AxisChoiceStatistics choice = brick2012::axisChoiceStatistics(layer);
    CHECK(choice.pointerCellsPaperRule == 0);
    CHECK(choice.bricksWhereBestDiffers == 0);
}
