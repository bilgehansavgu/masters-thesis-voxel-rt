// ============================================================================
// READING ORDER: FILE 9
// ============================================================================
// PURPOSE:
// Implements `IEncoding` for both bit layouts of the 2012 Reichl et al. format:
//   - `brick2012_inline`: attributes next to heights (32-bit cells, 4 buffers)
//   - `brick2012_split`:  attributes in their own buffer (16-bit cells, 5 buffers)
//
// Both store the exact same columns and runs (`ColumnBrick`); only the placement
// of the 16-bit octahedral normals differs. All bit shifts and masks come from
// `shared/include/thesis/shared/Brick2012.h`.
// ============================================================================

#include "thesis/brick2012/ColumnBrick.h"
#include "thesis/encoding/Encoding.h"
#include "thesis/shared/Bindings.h"
#include "thesis/shared/Brick2012.h"

#include <glm/common.hpp>

#include <algorithm>
#include <cstring>
#include <execution>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>

namespace thesis::brick2012 {
namespace {

enum class Layout : std::uint32_t {
    Inline = THESIS_BRICK_LAYOUT_INLINE,
    Split = THESIS_BRICK_LAYOUT_SPLIT,
};

// Buffer "header" (binding 9): facts about the whole model. std430 layout: 3 x 16 = 48 bytes.
struct BrickModelHeader {
    glm::vec3 origin;
    float voxelSize;
    glm::uvec3 gridSize;
    std::uint32_t brickSize;
    std::uint32_t brickCount;
    std::uint32_t layout;
    std::uint32_t reserved0;
    std::uint32_t reserved1;
};
static_assert(sizeof(BrickModelHeader) == 48);

// Buffer "brick table" (binding 10): one 16-byte record per brick, indexed by `primitiveIndex`.
// A tracer reaches the data of a brick ONLY through this record, which decouples the brick's
// ID from where its cells/indirection/attributes live in memory (essential for on-demand
// brick streaming in later steps).
struct BrickRecord {
    std::uint32_t position;        // brick coordinate (x,y,z in 10 bits each) + primary axis (bits 30..31)
    std::uint32_t cellBase;        // index of the brick's first cell in the cell buffer
    std::uint32_t indirectionBase; // index of the brick's first 16-bit word in the indirection buffer
    std::uint32_t attributeBase;   // split layout: index of the brick's first 16-bit normal in attributes
};
static_assert(sizeof(BrickRecord) == 16);

// Because each brick coordinate (x, y, z) is packed into 10 bits in `BrickRecord::position`,
// a grid can have at most 2^10 = 1024 bricks per axis (e.g., 8192^3 voxels with 8^3 bricks).
constexpr std::uint32_t kMaxBricksPerAxis = 1u << THESIS_BRICK_COORDINATE_BITS;

// Packs a `Run{start, length}` into a 16-bit indirection word:
//   bits 0..5:  start
//   bits 6..11: length - 1 (since length >= 1)
//   bit 12:     1 if this is the last run in the column, 0 otherwise
std::uint16_t packRun(const Run& run, bool last)
{
    return static_cast<std::uint16_t>(run.start | ((run.length - 1u) << THESIS_RUN_LENGTH_SHIFT) |
                                      (last ? THESIS_RUN_LAST_BIT : 0u));
}

// Unpacks a 16-bit indirection word back into `Run{start, length}`.
Run unpackRun(std::uint16_t word)
{
    return {static_cast<std::uint8_t>(word & THESIS_RUN_START_MASK),
            static_cast<std::uint8_t>(((word >> THESIS_RUN_LENGTH_SHIFT) & THESIS_RUN_LENGTH_MASK) + 1u)};
}

// Intermediate result for one brick encoded independently on a worker thread.
// Offsets inside `EncodedBrick` count from 0 (the start of THIS brick's data);
// when all bricks are concatenated into global GPU buffers, `BrickRecord` stores
// the brick's global base indices (`cellBase`, `indirectionBase`, `attributeBase`).
//
// C++ CONCURRENCY NOTE: Why `const char* error` instead of `throw`?
// In C++, if an exception escapes a lambda inside `std::for_each(std::execution::par, ...)`,
// the C++ standard requires calling `std::terminate()` immediately! To report errors safely
// from parallel worker threads, we record `out.error` and throw on the main thread after
// the parallel loop finishes.
struct EncodedBrick {
    std::uint32_t axis = 0;
    core::Aabb bounds{};
    std::vector<std::uint32_t> cells; // one value per cell; written as 16 or 32 bits
    std::vector<std::uint16_t> indirection;
    std::vector<std::uint16_t> attributes; // split layout only
    ColumnStatistics statistics;
    const char* error = nullptr; // set instead of throwing: this runs inside a parallel loop
};

// ============================================================================
// INLINE ENCODER (`brick2012_inline`)
// ============================================================================
// - EMPTY (0 voxels):   cell = 0
// - HEIGHT (1 voxel):   cell packs kind=1, height (bits 2..7), and 16-bit normal (bits 8..23).
// - POINTER (>1 voxel): cell packs kind=2 and the brick-local offset into `out.indirection`.
//                       Appends all packed run words, followed immediately by all voxel normals
//                       for that column, into `out.indirection`.
void encodeInline(const ColumnBrick& columns, EncodedBrick& out)
{
    for (std::uint32_t cell = 0; cell < columns.cellCount(); ++cell) {
        const std::span<const Run> runs = columns.runs(cell);
        const std::span<const std::uint16_t> normals = columns.normals(cell);
        if (normals.empty()) {
            out.cells.push_back(THESIS_CELL_EMPTY);
        } else if (normals.size() == 1) {
            out.cells.push_back(THESIS_CELL_HEIGHT |
                                (static_cast<std::uint32_t>(runs[0].start) << THESIS_CELL_HEIGHT_SHIFT) |
                                (static_cast<std::uint32_t>(normals[0]) << THESIS_CELL_NORMAL_SHIFT));
        } else {
            const std::size_t offset = out.indirection.size();
            if (offset > THESIS_CELL_OFFSET_MAX_INLINE) {
                out.error = "has more run data than a pointer cell can address";
                return;
            }
            out.cells.push_back(THESIS_CELL_POINTER | (static_cast<std::uint32_t>(offset) << THESIS_CELL_OFFSET_SHIFT));
            for (std::size_t i = 0; i < runs.size(); ++i) {
                out.indirection.push_back(packRun(runs[i], i + 1 == runs.size()));
            }
            out.indirection.insert(out.indirection.end(), normals.begin(), normals.end());
        }
    }
}

// ============================================================================
// SPLIT ENCODER (`brick2012_split`)
// ============================================================================
// Keeps cells 16-bit and puts all normals into `out.attributes`:
// - All HEIGHT-cell normals come first in `out.attributes`, followed by all POINTER-column
//   normals (`pointerNormals`).
// - The first N words of `out.indirection` store the row prefix sums:
//   `out.indirection[v]` = how many HEIGHT cells appeared in rows `0 .. v-1`.
// - Each POINTER column writes 1 word (`firstNormal` index in `out.attributes`) followed
//   by its packed run words into `out.indirection`.
void encodeSplit(const ColumnBrick& columns, EncodedBrick& out)
{
    const std::uint32_t n = columns.brickSize;
    std::uint32_t heightCellsTotal = 0;
    for (std::uint32_t cell = 0; cell < columns.cellCount(); ++cell) {
        heightCellsTotal += columns.normals(cell).size() == 1 ? 1u : 0u;
    }

    out.indirection.assign(n, 0); // word v: number of height cells in the rows before row v
    std::vector<std::uint16_t> pointerNormals;
    std::uint32_t heightCellsSoFar = 0;
    for (std::uint32_t cell = 0; cell < columns.cellCount(); ++cell) {
        if (cell % n == 0) {
            out.indirection[cell / n] = static_cast<std::uint16_t>(heightCellsSoFar);
        }
        const std::span<const Run> runs = columns.runs(cell);
        const std::span<const std::uint16_t> normals = columns.normals(cell);
        if (normals.empty()) {
            out.cells.push_back(THESIS_CELL_EMPTY);
        } else if (normals.size() == 1) {
            out.cells.push_back(THESIS_CELL_HEIGHT |
                                (static_cast<std::uint32_t>(runs[0].start) << THESIS_CELL_HEIGHT_SHIFT));
            out.attributes.push_back(normals[0]);
            ++heightCellsSoFar;
        } else {
            const std::size_t offset = out.indirection.size();
            if (offset > THESIS_CELL_OFFSET_MAX_SPLIT) {
                out.error = "has more run data than a 16-bit pointer cell can address";
                return;
            }
            out.cells.push_back(THESIS_CELL_POINTER | (static_cast<std::uint32_t>(offset) << THESIS_CELL_OFFSET_SHIFT));
            // Index of the column's first normal: after all height cells and earlier columns.
            out.indirection.push_back(static_cast<std::uint16_t>(heightCellsTotal + pointerNormals.size()));
            for (std::size_t i = 0; i < runs.size(); ++i) {
                out.indirection.push_back(packRun(runs[i], i + 1 == runs.size()));
            }
            pointerNormals.insert(pointerNormals.end(), normals.begin(), normals.end());
        }
    }
    out.attributes.insert(out.attributes.end(), pointerNormals.begin(), pointerNormals.end());
}

// Computes the tight world-space Axis-Aligned Bounding Box (AABB) around ONLY the filled
// voxels of a brick (rather than the full N x N x N cube). Tight bounds make ray-AABB tests
// skip empty borders around partially filled bricks!
core::Aabb boundsOf(const ColumnBrick& columns, const glm::uvec3& coordinate, const voxel::VoxelGrid& grid)
{
    const std::uint32_t n = columns.brickSize;
    glm::uvec3 lo(n);
    glm::uvec3 hi(0);
    for (std::uint32_t cell = 0; cell < columns.cellCount(); ++cell) {
        const std::span<const Run> runs = columns.runs(cell);
        if (runs.empty()) {
            continue;
        }
        const std::uint32_t top = runs.back().start + runs.back().length - 1u;
        lo = glm::min(lo, voxelOfColumn(columns.axis, cell % n, cell / n, runs.front().start));
        hi = glm::max(hi, voxelOfColumn(columns.axis, cell % n, cell / n, top));
    }
    const glm::uvec3 first = coordinate * n;
    return core::Aabb{grid.origin + grid.voxelSize * glm::vec3(first + lo),
                      grid.origin + grid.voxelSize * glm::vec3(first + hi + 1u)};
}

// Copies raw array bytes into a `std::vector<std::byte>` padded to a multiple of 4 bytes
// (and at least 4 bytes), because Vulkan storage buffers (`StructuredBuffer<uint>`) are
// indexed in 32-bit words and cannot have size 0.
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
    throw std::runtime_error("brick2012: scene '" + scene.name + "' has no buffer at binding " +
                             std::to_string(binding));
}

template <typename T>
T valueAt(const std::vector<std::byte>& bytes, std::uint64_t index)
{
    if ((index + 1) * sizeof(T) > bytes.size()) {
        throw std::runtime_error("brick2012: the encoded data ends too early");
    }
    T value;
    std::memcpy(&value, bytes.data() + index * sizeof(T), sizeof(T));
    return value;
}

class Brick2012Encoding final : public encoding::IEncoding {
public:
    explicit Brick2012Encoding(Layout layout) : layout_(layout) {}

    std::string_view name() const override
    {
        return layout_ == Layout::Inline ? "brick2012_inline" : "brick2012_split";
    }

    encoding::EncodedModel encode(const voxel::VoxelModel& model) const override
    {
        model.validate();
        const std::uint32_t n = model.grid.brickSize;
        const glm::uvec3 bricksPerAxis = model.grid.size / n;
        if (std::max({bricksPerAxis.x, bricksPerAxis.y, bricksPerAxis.z}) > kMaxBricksPerAxis) {
            throw std::runtime_error(std::string(name()) + ": the grid has more than " +
                                     std::to_string(kMaxBricksPerAxis) + " bricks along one axis");
        }

        // 1. Encode every brick on its own, in parallel.
        std::vector<EncodedBrick> encoded(model.bricks.size());
        std::vector<std::size_t> order(model.bricks.size());
        std::iota(order.begin(), order.end(), std::size_t{0});
        std::for_each(std::execution::par, order.begin(), order.end(), [&](std::size_t i) {
            const voxel::VoxelBrick& brick = model.bricks[i];
            EncodedBrick& out = encoded[i];
            out.axis = choosePrimaryAxis(brick);
            const ColumnBrick columns = toColumns(brick, n, out.axis);
            out.statistics.add(columns);
            out.bounds = boundsOf(columns, brick.coordinate, model.grid);
            if (layout_ == Layout::Inline) {
                encodeInline(columns, out);
            } else {
                encodeSplit(columns, out);
            }
        });

        // 2. Join the bricks into the buffers and note where each brick starts.
        const std::uint64_t cellsPerBrick = static_cast<std::uint64_t>(n) * n;
        std::uint64_t indirectionWords = 0;
        std::uint64_t attributeWords = 0;
        for (std::size_t i = 0; i < encoded.size(); ++i) {
            if (encoded[i].error != nullptr) {
                const glm::uvec3& at = model.bricks[i].coordinate;
                throw std::runtime_error(std::string(name()) + ": brick (" + std::to_string(at.x) + ", " +
                                         std::to_string(at.y) + ", " + std::to_string(at.z) + ") " + encoded[i].error);
            }
            indirectionWords += encoded[i].indirection.size();
            attributeWords += encoded[i].attributes.size();
        }
        if (std::max({cellsPerBrick * encoded.size(), indirectionWords, attributeWords}) > 0xFFFFFFFFull) {
            throw std::runtime_error(std::string(name()) + ": the model is too large for 32-bit offsets");
        }

        const std::size_t cellBytes = layout_ == Layout::Inline ? 4 : 2;
        std::vector<BrickRecord> table;
        std::vector<std::byte> cells(static_cast<std::size_t>(cellsPerBrick) * encoded.size() * cellBytes);
        std::vector<std::uint16_t> indirection;
        std::vector<std::uint16_t> attributes;
        table.reserve(encoded.size());
        indirection.reserve(static_cast<std::size_t>(indirectionWords));
        attributes.reserve(static_cast<std::size_t>(attributeWords));

        encoding::EncodedModel result;
        ColumnStatistics statistics;
        core::Scene& scene = result.scene;
        scene.name = model.name;
        scene.primitiveKind = std::string(name());
        scene.primitiveBounds.reserve(encoded.size());
        for (std::size_t i = 0; i < encoded.size(); ++i) {
            const EncodedBrick& brick = encoded[i];
            const glm::uvec3& at = model.bricks[i].coordinate;
            const auto cellBase = static_cast<std::uint32_t>(cellsPerBrick * i);
            table.push_back({at.x | (at.y << THESIS_BRICK_COORDINATE_BITS) |
                                 (at.z << (2 * THESIS_BRICK_COORDINATE_BITS)) | (brick.axis << THESIS_BRICK_AXIS_SHIFT),
                             cellBase, static_cast<std::uint32_t>(indirection.size()),
                             static_cast<std::uint32_t>(attributes.size())});

            std::byte* target = cells.data() + static_cast<std::size_t>(cellBase) * cellBytes;
            if (layout_ == Layout::Inline) {
                std::memcpy(target, brick.cells.data(), brick.cells.size() * sizeof(std::uint32_t));
            } else {
                for (std::size_t cell = 0; cell < brick.cells.size(); ++cell) {
                    const auto value = static_cast<std::uint16_t>(brick.cells[cell]);
                    std::memcpy(target + cell * sizeof(value), &value, sizeof(value));
                }
            }
            indirection.insert(indirection.end(), brick.indirection.begin(), brick.indirection.end());
            attributes.insert(attributes.end(), brick.attributes.begin(), brick.attributes.end());

            scene.primitiveBounds.push_back(brick.bounds);
            scene.bounds.extend(brick.bounds);
            statistics.add(brick.statistics);
        }

        const BrickModelHeader header{model.grid.origin,
                                      model.grid.voxelSize,
                                      model.grid.size,
                                      n,
                                      static_cast<std::uint32_t>(encoded.size()),
                                      static_cast<std::uint32_t>(layout_),
                                      0,
                                      0};
        scene.primitiveData.push_back({THESIS_BINDING_BRICK_HEADER, "header", toBytes(&header, sizeof(header))});
        scene.primitiveData.push_back(
            {THESIS_BINDING_BRICK_TABLE, "brick table", toBytes(table.data(), table.size() * sizeof(BrickRecord))});
        scene.primitiveData.push_back({THESIS_BINDING_BRICK_CELLS, "cells", std::move(cells)});
        scene.primitiveData.push_back({THESIS_BINDING_BRICK_INDIRECTION, "indirection",
                                       toBytes(indirection.data(), indirection.size() * sizeof(std::uint16_t))});
        if (layout_ == Layout::Split) {
            scene.primitiveData.push_back({THESIS_BINDING_BRICK_ATTRIBUTES, "attributes",
                                           toBytes(attributes.data(), attributes.size() * sizeof(std::uint16_t))});
        }

        result.statistics = statistics.toJson();
        result.statistics["encoding"] = name();
        result.statistics["brick_size"] = n;
        result.statistics["memory"] = encoding::memoryReport(scene).toJson(statistics.voxels);
        return result;
    }

    // Reconstructs the exact `VoxelModel` from the raw GPU buffers in `scene`.
    // Note that in `decodeSplit`, finding a HEIGHT cell's normal uses the exact same algorithm
    // that the GPU shader will use in Step 2: reading the row's prefix sum from `indirection`
    // and counting earlier HEIGHT cells in the same row!
    voxel::VoxelModel decode(const core::Scene& scene) const override
    {
        const std::vector<std::byte>& headerBytes = bufferAt(scene, THESIS_BINDING_BRICK_HEADER);
        const std::vector<std::byte>& table = bufferAt(scene, THESIS_BINDING_BRICK_TABLE);
        const std::vector<std::byte>& cells = bufferAt(scene, THESIS_BINDING_BRICK_CELLS);
        const std::vector<std::byte>& indirection = bufferAt(scene, THESIS_BINDING_BRICK_INDIRECTION);
        const std::vector<std::byte>* attributes =
            layout_ == Layout::Split ? &bufferAt(scene, THESIS_BINDING_BRICK_ATTRIBUTES) : nullptr;

        const auto header = valueAt<BrickModelHeader>(headerBytes, 0);
        if (header.layout != static_cast<std::uint32_t>(layout_) || !voxel::isSupportedBrickSize(header.brickSize)) {
            throw std::runtime_error(std::string(name()) + ": scene '" + scene.name + "' is not in this encoding");
        }
        const std::uint32_t n = header.brickSize;

        voxel::VoxelModel model;
        model.name = scene.name;
        model.grid = {header.origin, header.voxelSize, header.gridSize, n};
        model.bricks.resize(header.brickCount);
        std::vector<std::string> errors(header.brickCount);
        std::vector<std::size_t> order(header.brickCount);
        std::iota(order.begin(), order.end(), std::size_t{0});
        std::for_each(std::execution::par, order.begin(), order.end(), [&](std::size_t i) {
            try {
                const auto record = valueAt<BrickRecord>(table, i);
                const glm::uvec3 coordinate(
                    record.position & THESIS_BRICK_COORDINATE_MASK,
                    (record.position >> THESIS_BRICK_COORDINATE_BITS) & THESIS_BRICK_COORDINATE_MASK,
                    (record.position >> (2 * THESIS_BRICK_COORDINATE_BITS)) & THESIS_BRICK_COORDINATE_MASK);
                ColumnBrick columns;
                columns.brickSize = n;
                columns.axis = record.position >> THESIS_BRICK_AXIS_SHIFT;
                if (columns.axis > 2) {
                    throw std::runtime_error(std::string(name()) + ": a brick record has an invalid primary axis");
                }
                if (layout_ == Layout::Inline) {
                    decodeInline(record, cells, indirection, columns);
                } else {
                    decodeSplit(record, cells, indirection, *attributes, columns);
                }
                model.bricks[i] = fromColumns(columns, coordinate);
            } catch (const std::exception& error) {
                errors[i] = error.what();
            }
        });
        for (const std::string& error : errors) {
            if (!error.empty()) {
                throw std::runtime_error(error);
            }
        }
        return model;
    }

    nlohmann::json analyze(const voxel::VoxelModel& model) const override
    {
        return {{"primary_axis_choice", axisChoiceStatistics(model).toJson()}};
    }

private:
    // Reads the run words of one column, starting at `word`. Returns the number of voxels.
    static std::uint32_t readRuns(const std::vector<std::byte>& indirection, std::uint64_t& word,
                                  std::vector<Run>& runs)
    {
        std::uint32_t voxels = 0;
        for (;;) {
            const auto packed = valueAt<std::uint16_t>(indirection, word++);
            runs.push_back(unpackRun(packed));
            voxels += runs.back().length;
            if ((packed & THESIS_RUN_LAST_BIT) != 0) {
                return voxels;
            }
        }
    }

    static void decodeInline(const BrickRecord& record, const std::vector<std::byte>& cells,
                             const std::vector<std::byte>& indirection, ColumnBrick& columns)
    {
        std::vector<Run> runs;
        std::vector<std::uint16_t> normals;
        for (std::uint32_t cell = 0; cell < columns.cellCount(); ++cell) {
            runs.clear();
            normals.clear();
            const auto value = valueAt<std::uint32_t>(cells, static_cast<std::uint64_t>(record.cellBase) + cell);
            const std::uint32_t kind = value & THESIS_CELL_KIND_MASK;
            if (kind == THESIS_CELL_HEIGHT) {
                runs.push_back(
                    {static_cast<std::uint8_t>((value >> THESIS_CELL_HEIGHT_SHIFT) & THESIS_CELL_HEIGHT_MASK), 1});
                normals.push_back(
                    static_cast<std::uint16_t>((value >> THESIS_CELL_NORMAL_SHIFT) & THESIS_CELL_NORMAL_MASK));
            } else if (kind == THESIS_CELL_POINTER) {
                std::uint64_t word =
                    static_cast<std::uint64_t>(record.indirectionBase) + (value >> THESIS_CELL_OFFSET_SHIFT);
                const std::uint32_t voxels = readRuns(indirection, word, runs);
                for (std::uint32_t k = 0; k < voxels; ++k) {
                    normals.push_back(valueAt<std::uint16_t>(indirection, word + k));
                }
            } else if (kind != THESIS_CELL_EMPTY) {
                throw std::runtime_error("brick2012_inline: a cell has an unknown kind");
            }
            columns.appendCell(runs, normals);
        }
    }

    // Finds every normal the way a shader has to: through the row words and the pointer words.
    static void decodeSplit(const BrickRecord& record, const std::vector<std::byte>& cells,
                            const std::vector<std::byte>& indirection, const std::vector<std::byte>& attributes,
                            ColumnBrick& columns)
    {
        const std::uint32_t n = columns.brickSize;
        const auto cellAt = [&](std::uint32_t cell) {
            return valueAt<std::uint16_t>(cells, static_cast<std::uint64_t>(record.cellBase) + cell);
        };
        const auto normalAt = [&](std::uint64_t index) {
            return valueAt<std::uint16_t>(attributes, static_cast<std::uint64_t>(record.attributeBase) + index);
        };

        std::vector<Run> runs;
        std::vector<std::uint16_t> normals;
        for (std::uint32_t cell = 0; cell < columns.cellCount(); ++cell) {
            runs.clear();
            normals.clear();
            const std::uint32_t value = cellAt(cell);
            const std::uint32_t kind = value & THESIS_CELL_KIND_MASK;
            if (kind == THESIS_CELL_HEIGHT) {
                const std::uint32_t row = cell / n;
                std::uint32_t index =
                    valueAt<std::uint16_t>(indirection, static_cast<std::uint64_t>(record.indirectionBase) + row);
                for (std::uint32_t earlier = row * n; earlier < cell; ++earlier) {
                    index += (cellAt(earlier) & THESIS_CELL_KIND_MASK) == THESIS_CELL_HEIGHT ? 1u : 0u;
                }
                runs.push_back(
                    {static_cast<std::uint8_t>((value >> THESIS_CELL_HEIGHT_SHIFT) & THESIS_CELL_HEIGHT_MASK), 1});
                normals.push_back(normalAt(index));
            } else if (kind == THESIS_CELL_POINTER) {
                std::uint64_t word =
                    static_cast<std::uint64_t>(record.indirectionBase) + (value >> THESIS_CELL_OFFSET_SHIFT);
                const std::uint32_t firstNormal = valueAt<std::uint16_t>(indirection, word++);
                const std::uint32_t voxels = readRuns(indirection, word, runs);
                for (std::uint32_t k = 0; k < voxels; ++k) {
                    normals.push_back(normalAt(static_cast<std::uint64_t>(firstNormal) + k));
                }
            } else if (kind != THESIS_CELL_EMPTY) {
                throw std::runtime_error("brick2012_split: a cell has an unknown kind");
            }
            columns.appendCell(runs, normals);
        }
    }

    Layout layout_;
};

} // namespace
} // namespace thesis::brick2012

namespace thesis::plugins {

void registerBrick2012Encodings(encoding::EncodingRegistry& registry)
{
    registry.add("brick2012_inline", "2012 brick format, attributes next to the heights (the order of the paper).",
                 [] { return std::make_unique<brick2012::Brick2012Encoding>(brick2012::Layout::Inline); });
    registry.add("brick2012_split", "2012 brick format, attributes in their own buffer.",
                 [] { return std::make_unique<brick2012::Brick2012Encoding>(brick2012::Layout::Split); });
}

} // namespace thesis::plugins
