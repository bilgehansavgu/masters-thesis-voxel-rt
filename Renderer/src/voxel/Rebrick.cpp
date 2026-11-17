#include "thesis/voxel/Rebrick.h"

#include <glm/vec3.hpp>

#include <algorithm>
#include <bit>
#include <execution>
#include <iterator>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>

namespace thesis::voxel {
namespace {

// A row is the voxels of a brick along x, for one y and one z. Brick sizes are 8, 16 or 32, so a
// row, or an aligned part of a row, lies inside one 64-bit word of the occupancy.
std::uint64_t rowBits(const std::vector<std::uint64_t>& occupancy, std::uint32_t firstBit, std::uint32_t length)
{
    return (occupancy[firstBit >> 6] >> (firstBit & 63)) & ((1ull << length) - 1ull);
}

// For each 64-bit word of the occupancy: the number of filled voxels before it.
std::vector<std::uint32_t> filledBeforeWord(const std::vector<std::uint64_t>& occupancy)
{
    std::vector<std::uint32_t> before(occupancy.size());
    std::uint32_t count = 0;
    for (std::size_t word = 0; word < occupancy.size(); ++word) {
        before[word] = count;
        count += static_cast<std::uint32_t>(std::popcount(occupancy[word]));
    }
    return before;
}

// The number of filled voxels before bit `bit`: the place of its normal in the brick.
std::uint32_t filledBefore(const std::vector<std::uint64_t>& occupancy, const std::vector<std::uint32_t>& beforeWord,
                           std::uint32_t bit)
{
    const std::uint64_t lower = occupancy[bit >> 6] & ((1ull << (bit & 63)) - 1ull);
    return beforeWord[bit >> 6] + static_cast<std::uint32_t>(std::popcount(lower));
}

// The place of a brick in the order that VoxelModel requires.
std::uint64_t orderOf(const glm::uvec3& coordinate, const glm::uvec3& bricksPerAxis)
{
    return coordinate.x + static_cast<std::uint64_t>(bricksPerAxis.x) *
                              (coordinate.y + static_cast<std::uint64_t>(bricksPerAxis.y) * coordinate.z);
}

// Smaller bricks: every brick of the model is cut into (from / to)^3 pieces.
std::vector<VoxelBrick> split(const VoxelModel& model, std::uint32_t to, const glm::uvec3& bricksPerAxis)
{
    const std::uint32_t from = model.grid.brickSize;
    const std::uint32_t parts = from / to;

    std::vector<std::vector<VoxelBrick>> pieces(model.bricks.size());
    std::vector<std::size_t> indices(model.bricks.size());
    std::iota(indices.begin(), indices.end(), std::size_t{0});
    std::for_each(std::execution::par, indices.begin(), indices.end(), [&](std::size_t index) {
        const VoxelBrick& source = model.bricks[index];
        const std::vector<std::uint32_t> beforeWord = filledBeforeWord(source.occupancy);
        for (std::uint32_t pz = 0; pz < parts; ++pz) {
            for (std::uint32_t py = 0; py < parts; ++py) {
                for (std::uint32_t px = 0; px < parts; ++px) {
                    VoxelBrick piece;
                    piece.coordinate = source.coordinate * parts + glm::uvec3(px, py, pz);
                    piece.occupancy.assign(voxelsPerBrick(to) / 64, 0);
                    // Rows in ascending z, then y: this is the bit order of the piece, so the
                    // normals arrive in the order that the piece needs.
                    for (std::uint32_t z = 0; z < to; ++z) {
                        for (std::uint32_t y = 0; y < to; ++y) {
                            const std::uint32_t sourceRow = voxelIndex(from, px * to, py * to + y, pz * to + z);
                            const std::uint64_t bits = rowBits(source.occupancy, sourceRow, to);
                            if (bits == 0) {
                                continue;
                            }
                            const std::uint32_t row = voxelIndex(to, 0, y, z);
                            piece.occupancy[row >> 6] |= bits << (row & 63);
                            const auto first =
                                source.normals.begin() + filledBefore(source.occupancy, beforeWord, sourceRow);
                            piece.normals.insert(piece.normals.end(), first, first + std::popcount(bits));
                        }
                    }
                    if (!piece.normals.empty()) {
                        pieces[index].push_back(std::move(piece));
                    }
                }
            }
        }
    });

    std::vector<VoxelBrick> bricks;
    for (std::vector<VoxelBrick>& list : pieces) {
        std::move(list.begin(), list.end(), std::back_inserter(bricks));
    }
    std::sort(bricks.begin(), bricks.end(), [&](const VoxelBrick& a, const VoxelBrick& b) {
        return orderOf(a.coordinate, bricksPerAxis) < orderOf(b.coordinate, bricksPerAxis);
    });
    return bricks;
}

// Larger bricks: up to (to / from)^3 bricks of the model become one.
std::vector<VoxelBrick> merge(const VoxelModel& model, std::uint32_t to, const glm::uvec3& bricksPerAxis)
{
    const std::uint32_t from = model.grid.brickSize;
    const std::uint32_t parts = to / from;

    // The bricks of the model, sorted by the new brick that they belong to.
    struct Member {
        std::uint64_t order;
        std::size_t source;
    };
    std::vector<Member> members;
    members.reserve(model.bricks.size());
    for (std::size_t index = 0; index < model.bricks.size(); ++index) {
        members.push_back({orderOf(model.bricks[index].coordinate / parts, bricksPerAxis), index});
    }
    std::stable_sort(members.begin(), members.end(),
                     [](const Member& a, const Member& b) { return a.order < b.order; });

    std::vector<std::size_t> groupStart; // index into `members` of the first member of each new brick
    for (std::size_t member = 0; member < members.size(); ++member) {
        if (member == 0 || members[member].order != members[member - 1].order) {
            groupStart.push_back(member);
        }
    }

    std::vector<VoxelBrick> bricks(groupStart.size());
    std::vector<std::size_t> groups(groupStart.size());
    std::iota(groups.begin(), groups.end(), std::size_t{0});
    std::for_each(std::execution::par, groups.begin(), groups.end(), [&](std::size_t group) {
        const std::size_t begin = groupStart[group];
        const std::size_t end = group + 1 < groupStart.size() ? groupStart[group + 1] : members.size();
        VoxelBrick& brick = bricks[group];
        brick.coordinate = model.bricks[members[begin].source].coordinate / parts;
        brick.occupancy.assign(voxelsPerBrick(to) / 64, 0);

        // Calls visit(bits of the row, first bit of the row in the new brick) for every row
        // with a filled voxel, in ascending order inside each brick of the model.
        const auto forEachRow = [&](const VoxelBrick& source, const auto& visit) {
            const glm::uvec3 offset = (source.coordinate % parts) * from;
            for (std::uint32_t z = 0; z < from; ++z) {
                for (std::uint32_t y = 0; y < from; ++y) {
                    const std::uint64_t bits = rowBits(source.occupancy, voxelIndex(from, 0, y, z), from);
                    if (bits != 0) {
                        visit(bits, voxelIndex(to, offset.x, offset.y + y, offset.z + z));
                    }
                }
            }
        };

        // 1. The bits. 2. The normals: the bits say where each normal belongs.
        std::size_t voxels = 0;
        for (std::size_t member = begin; member < end; ++member) {
            const VoxelBrick& source = model.bricks[members[member].source];
            forEachRow(source,
                       [&](std::uint64_t bits, std::uint32_t row) { brick.occupancy[row >> 6] |= bits << (row & 63); });
            voxels += source.normals.size();
        }
        const std::vector<std::uint32_t> beforeWord = filledBeforeWord(brick.occupancy);
        brick.normals.resize(voxels);
        for (std::size_t member = begin; member < end; ++member) {
            const VoxelBrick& source = model.bricks[members[member].source];
            std::size_t next = 0; // the normals of `source` are used up in ascending bit order
            forEachRow(source, [&](std::uint64_t bits, std::uint32_t row) {
                const auto count = static_cast<std::size_t>(std::popcount(bits));
                std::copy_n(source.normals.begin() + static_cast<std::ptrdiff_t>(next), count,
                            brick.normals.begin() + filledBefore(brick.occupancy, beforeWord, row));
                next += count;
            });
        }
    });
    return bricks;
}

} // namespace

VoxelModel rebrick(const VoxelModel& model, std::uint32_t brickSize)
{
    if (!isSupportedBrickSize(brickSize)) {
        throw std::invalid_argument("rebrick: brick size " + std::to_string(brickSize) +
                                    " is not supported (8, 16 or 32)");
    }
    if (brickSize == model.grid.brickSize) {
        return model;
    }

    VoxelModel result;
    result.name = model.name;
    result.source = model.source;
    result.grid = model.grid;
    result.grid.brickSize = brickSize;
    result.grid.size = (model.grid.size + (brickSize - 1u)) / brickSize * brickSize;
    const glm::uvec3 bricksPerAxis = result.grid.size / brickSize;
    result.bricks = brickSize < model.grid.brickSize ? split(model, brickSize, bricksPerAxis)
                                                     : merge(model, brickSize, bricksPerAxis);
    return result;
}

} // namespace thesis::voxel
