// The baseline brick format: one occupancy bit for every voxel of a brick and one normal per
// filled voxel. It is what a brick costs without the 2012 format, so the memory of the 2012
// format is measured against it, and its brick test is the plain walk through those bits.
//
//   bitmask   occupancy bits, row counts to find a normal, normals
//
// Every bit and binding is defined in thesis/bitmask/BitmaskShared.h, which the shader
// (shaders/primitives/bitmask) reads as well.

#include "thesis/bitmask/BitmaskBuffers.h"
#include "thesis/bitmask/BitmaskShared.h"
#include "thesis/encoding/Encoding.h"

#include <glm/common.hpp>

#include <algorithm>
#include <bit>
#include <cstring>
#include <execution>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>

namespace thesis::bitmask {
namespace {

constexpr const char* kName = "bitmask";
constexpr std::uint32_t kMaxBricksPerAxis = 1u << THESIS_BITMASK_COORDINATE_BITS;

// The N occupancy bits of column `cell` of a brick: bit h is voxel (h, u, v) with cell = u + N * v.
std::uint32_t columnBits(const std::vector<std::uint64_t>& occupancy, std::uint32_t n, std::uint32_t cell)
{
    const std::uint32_t bit = n * cell;
    const std::uint64_t mask = (1ull << n) - 1ull; // n <= 32
    return static_cast<std::uint32_t>((occupancy[bit >> 6] >> (bit & 63)) & mask);
}

// The box around the filled voxels of a brick, in world space.
core::Aabb boundsOf(const voxel::VoxelBrick& brick, std::uint32_t n, const voxel::VoxelGrid& grid)
{
    glm::uvec3 lo(n);
    glm::uvec3 hi(0);
    for (std::uint32_t cell = 0; cell < n * n; ++cell) {
        const std::uint32_t bits = columnBits(brick.occupancy, n, cell);
        if (bits == 0) {
            continue;
        }
        const glm::uvec3 first(static_cast<std::uint32_t>(std::countr_zero(bits)), cell % n, cell / n);
        const glm::uvec3 last(31u - static_cast<std::uint32_t>(std::countl_zero(bits)), cell % n, cell / n);
        lo = glm::min(lo, first);
        hi = glm::max(hi, last);
    }
    const glm::uvec3 start = brick.coordinate * n;
    return core::Aabb{grid.origin + grid.voxelSize * glm::vec3(start + lo),
                      grid.origin + grid.voxelSize * glm::vec3(start + hi + 1u)};
}

// Word v: the filled voxels of the rows of cells before row v.
std::vector<std::uint16_t> rowCountsOf(const voxel::VoxelBrick& brick, std::uint32_t n)
{
    std::vector<std::uint16_t> counts(n, 0);
    std::uint32_t sum = 0;
    for (std::uint32_t row = 0; row < n; ++row) {
        counts[row] = static_cast<std::uint16_t>(sum); // at most 31 * 32 * 32 for N = 32
        for (std::uint32_t u = 0; u < n; ++u) {
            sum += static_cast<std::uint32_t>(std::popcount(columnBits(brick.occupancy, n, u + n * row)));
        }
    }
    return counts;
}

std::vector<std::byte> toBytes(const void* data, std::size_t bytes)
{
    // Padded to whole 32-bit words (a shader reads 32-bit words), and never empty.
    std::vector<std::byte> result(std::max<std::size_t>((bytes + 3) / 4 * 4, 4), std::byte{0});
    if (bytes > 0) {
        std::memcpy(result.data(), data, bytes);
    }
    return result;
}

const std::vector<std::byte>& bufferAt(const core::Scene& scene, std::uint32_t binding)
{
    for (const core::PrimitiveData& data : scene.primitiveData) {
        if (data.binding == binding) {
            return data.bytes;
        }
    }
    throw std::runtime_error(std::string(kName) + ": scene '" + scene.name + "' has no buffer at binding " +
                             std::to_string(binding));
}

template <typename T>
T valueAt(const std::vector<std::byte>& bytes, std::uint64_t index)
{
    if ((index + 1) * sizeof(T) > bytes.size()) {
        throw std::runtime_error(std::string(kName) + ": the encoded data ends too early");
    }
    T value;
    std::memcpy(&value, bytes.data() + index * sizeof(T), sizeof(T));
    return value;
}

class BitmaskEncoding final : public encoding::IEncoding {
public:
    std::string_view name() const override { return kName; }

    encoding::EncodedModel encode(const voxel::VoxelModel& model) const override
    {
        model.validate();
        const std::uint32_t n = model.grid.brickSize;
        const glm::uvec3 bricksPerAxis = model.grid.size / n;
        if (std::max({bricksPerAxis.x, bricksPerAxis.y, bricksPerAxis.z}) > kMaxBricksPerAxis) {
            throw std::runtime_error(std::string(kName) + ": the grid has more than " +
                                     std::to_string(kMaxBricksPerAxis) + " bricks along one axis");
        }
        const std::uint64_t maskWordsPerBrick = voxel::voxelsPerBrick(n) / 32u;
        if (maskWordsPerBrick * model.bricks.size() > 0xFFFFFFFFull || model.voxelCount() > 0xFFFFFFFFull) {
            throw std::runtime_error(std::string(kName) + ": the model is too large for 32-bit offsets");
        }

        encoding::EncodedModel result;
        core::Scene& scene = result.scene;
        scene.name = model.name;
        scene.primitiveKind = kName;
        // Every brick lies inside one cell of the brick grid.
        scene.primitiveGrid =
            core::RegularGrid{model.grid.origin, model.grid.voxelSize * static_cast<float>(n), bricksPerAxis};

        scene.primitiveBounds.resize(model.bricks.size());
        std::vector<std::size_t> order(model.bricks.size());
        std::iota(order.begin(), order.end(), std::size_t{0});
        std::for_each(std::execution::par, order.begin(), order.end(),
                      [&](std::size_t i) { scene.primitiveBounds[i] = boundsOf(model.bricks[i], n, model.grid); });

        std::vector<BitmaskBrickRecord> table;
        std::vector<std::uint64_t> masks;
        std::vector<std::uint16_t> rowCounts;
        std::vector<std::uint16_t> normals;
        table.reserve(model.bricks.size());
        masks.reserve(model.bricks.size() * maskWordsPerBrick / 2);
        rowCounts.reserve(model.bricks.size() * n);
        normals.reserve(model.voxelCount());
        for (std::size_t i = 0; i < model.bricks.size(); ++i) {
            const voxel::VoxelBrick& brick = model.bricks[i];
            const glm::uvec3& at = brick.coordinate;
            table.push_back(
                {at.x | (at.y << THESIS_BITMASK_COORDINATE_BITS) | (at.z << (2 * THESIS_BITMASK_COORDINATE_BITS)),
                 static_cast<std::uint32_t>(normals.size())});
            // The occupancy bits of the voxel model are the masks: bit N * cell + height.
            masks.insert(masks.end(), brick.occupancy.begin(), brick.occupancy.end());
            const std::vector<std::uint16_t> counts = rowCountsOf(brick, n);
            rowCounts.insert(rowCounts.end(), counts.begin(), counts.end());
            normals.insert(normals.end(), brick.normals.begin(), brick.normals.end());

            result.bricks.push_back({at, 0}); // columns along x
            scene.bounds.extend(scene.primitiveBounds[i]);
        }

        const BitmaskModelHeader header{model.grid.origin,
                                        model.grid.voxelSize,
                                        model.grid.size,
                                        n,
                                        static_cast<std::uint32_t>(model.bricks.size()),
                                        static_cast<std::uint32_t>(normals.size()),
                                        0,
                                        0};
        scene.primitiveData.push_back({THESIS_BINDING_BITMASK_HEADER, "header", toBytes(&header, sizeof(header))});
        scene.primitiveData.push_back({THESIS_BINDING_BITMASK_TABLE, "brick table",
                                       toBytes(table.data(), table.size() * sizeof(BitmaskBrickRecord))});
        scene.primitiveData.push_back(
            {THESIS_BINDING_BITMASK_MASKS, "masks", toBytes(masks.data(), masks.size() * sizeof(std::uint64_t))});
        scene.primitiveData.push_back({THESIS_BINDING_BITMASK_ROW_COUNTS, "row counts",
                                       toBytes(rowCounts.data(), rowCounts.size() * sizeof(std::uint16_t))});
        scene.primitiveData.push_back({THESIS_BINDING_BITMASK_NORMALS, "normals",
                                       toBytes(normals.data(), normals.size() * sizeof(std::uint16_t))});

        result.statistics["encoding"] = kName;
        result.statistics["brick_size"] = n;
        result.statistics["bricks"] = model.bricks.size();
        result.statistics["voxels"] = model.voxelCount();
        result.statistics["memory"] = encoding::memoryReport(scene).toJson(model.voxelCount());
        return result;
    }

    voxel::VoxelModel decode(const core::Scene& scene) const override
    {
        const auto header = valueAt<BitmaskModelHeader>(bufferAt(scene, THESIS_BINDING_BITMASK_HEADER), 0);
        const std::vector<std::byte>& table = bufferAt(scene, THESIS_BINDING_BITMASK_TABLE);
        const std::vector<std::byte>& masks = bufferAt(scene, THESIS_BINDING_BITMASK_MASKS);
        const std::vector<std::byte>& rowCounts = bufferAt(scene, THESIS_BINDING_BITMASK_ROW_COUNTS);
        const std::vector<std::byte>& normals = bufferAt(scene, THESIS_BINDING_BITMASK_NORMALS);
        if (!voxel::isSupportedBrickSize(header.brickSize)) {
            throw std::runtime_error(std::string(kName) + ": scene '" + scene.name + "' is not in this encoding");
        }
        const std::uint32_t n = header.brickSize;
        const std::uint64_t maskWordsPerBrick = voxel::voxelsPerBrick(n) / 64u;

        voxel::VoxelModel model;
        model.name = scene.name;
        model.grid = {header.origin, header.voxelSize, header.gridSize, n};
        model.bricks.resize(header.brickCount);
        std::uint64_t nextNormal = 0;
        for (std::uint32_t i = 0; i < header.brickCount; ++i) {
            const auto record = valueAt<BitmaskBrickRecord>(table, i);
            voxel::VoxelBrick& brick = model.bricks[i];
            brick.coordinate =
                glm::uvec3(record.position & THESIS_BITMASK_COORDINATE_MASK,
                           (record.position >> THESIS_BITMASK_COORDINATE_BITS) & THESIS_BITMASK_COORDINATE_MASK,
                           (record.position >> (2 * THESIS_BITMASK_COORDINATE_BITS)) & THESIS_BITMASK_COORDINATE_MASK);
            brick.occupancy.resize(maskWordsPerBrick);
            for (std::uint64_t w = 0; w < maskWordsPerBrick; ++w) {
                brick.occupancy[w] = valueAt<std::uint64_t>(masks, i * maskWordsPerBrick + w);
            }

            // The shader finds a normal through the record and the row counts, so they are checked
            // here against the masks: the round trip then proves them too.
            if (record.normalBase != nextNormal) {
                throw std::runtime_error(std::string(kName) + ": brick " + std::to_string(i) +
                                         " does not start at the next normal");
            }
            const std::vector<std::uint16_t> expectedCounts = rowCountsOf(brick, n);
            for (std::uint32_t row = 0; row < n; ++row) {
                if (valueAt<std::uint16_t>(rowCounts, static_cast<std::uint64_t>(i) * n + row) != expectedCounts[row]) {
                    throw std::runtime_error(std::string(kName) + ": brick " + std::to_string(i) +
                                             " has a wrong row count");
                }
            }
            const std::uint32_t filled = voxel::filledCount(brick.occupancy);
            brick.normals.resize(filled);
            for (std::uint32_t k = 0; k < filled; ++k) {
                brick.normals[k] = valueAt<std::uint16_t>(normals, nextNormal + k);
            }
            nextNormal += filled;
        }
        if (nextNormal != header.voxelCount) {
            throw std::runtime_error(std::string(kName) + ": the masks do not hold the voxel count of the header");
        }
        return model;
    }

    std::vector<encoding::EncodingSetting> settings() const override
    {
        return {{"Layout", "bitmask"}, {"Mipmap", "off"}};
    }
};

} // namespace
} // namespace thesis::bitmask

namespace thesis::plugins {

void registerBitmaskEncoding(encoding::EncodingRegistry& registry)
{
    registry.add("bitmask",
                 "Baseline, not the 2012 format: one occupancy bit for every voxel of a brick and a list of normals.",
                 [] { return std::make_unique<bitmask::BitmaskEncoding>(); });
}

} // namespace thesis::plugins
