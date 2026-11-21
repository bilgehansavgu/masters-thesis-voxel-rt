#pragma once

// Side: CPU

#include <glm/vec3.hpp>

#include <bit>
#include <cstdint>
#include <string>
#include <vector>

namespace thesis::voxel {

// Where the voxels are in the world. Voxel (x, y, z) is the cube from
// origin + voxelSize * (x, y, z) to origin + voxelSize * (x + 1, y + 1, z + 1).
struct VoxelGrid {
    glm::vec3 origin{0.0f};
    float voxelSize = 1.0f;
    glm::uvec3 size{0};           // voxels along each axis, a multiple of brickSize
    std::uint32_t brickSize = 32; // voxels along one side of a brick: 8, 16 or 32

    bool operator==(const VoxelGrid&) const = default;
};

// One cell of the regular brick grid, with the voxels inside it.
//
// `occupancy` has one bit per voxel of the brick: bit x + N * (y + N * z) is set when voxel
// (x, y, z) of the brick is filled, with N = brickSize. `normals` has one entry per filled voxel,
// in ascending bit order: the surface normal, packed into 16 bits (see OctNormal.h).
struct VoxelBrick {
    glm::uvec3 coordinate{0}; // position in bricks: the first voxel of the brick is coordinate * brickSize
    std::vector<std::uint64_t> occupancy;
    std::vector<std::uint16_t> normals;

    bool operator==(const VoxelBrick&) const = default;
};

// How the model was made, kept for reports.
struct VoxelSource {
    std::string mesh;
    std::uint64_t triangles = 0;
    std::string rule;             // voxelization rule, see Voxelizer.h
    std::uint32_t resolution = 0; // voxels along the longest axis of the mesh

    bool operator==(const VoxelSource&) const = default;
};

// THE VOXEL MODEL: the output of the voxelizer and the input of every encoding.
//
// It holds the filled voxels of a model in the plainest form (one bit per voxel), grouped into
// bricks. Only bricks with at least one filled voxel are stored, in ascending order of
// x + sizeX * (y + sizeY * z) of their coordinate. An encoding must reproduce this exactly:
// decode(encode(model)) == model is the test for every encoding.
struct VoxelModel {
    std::string name;
    VoxelGrid grid;
    VoxelSource source;
    std::vector<VoxelBrick> bricks;

    [[nodiscard]] std::uint64_t voxelCount() const;
    // Throws std::runtime_error when the model breaks one of the rules above.
    void validate() const;

    bool operator==(const VoxelModel&) const = default;
};

// True when both models have the same grid and the same voxels with the same normals.
// Name and source are not compared: an encoding does not store them.
[[nodiscard]] inline bool sameVoxels(const VoxelModel& a, const VoxelModel& b)
{
    return a.grid == b.grid && a.bricks == b.bricks;
}

[[nodiscard]] constexpr bool isSupportedBrickSize(std::uint32_t brickSize)
{
    return brickSize == 8 || brickSize == 16 || brickSize == 32;
}

[[nodiscard]] constexpr std::uint32_t voxelsPerBrick(std::uint32_t brickSize)
{
    return brickSize * brickSize * brickSize;
}

// Bit index of voxel (x, y, z) inside its brick.
[[nodiscard]] constexpr std::uint32_t voxelIndex(std::uint32_t brickSize, std::uint32_t x, std::uint32_t y,
                                                 std::uint32_t z)
{
    return x + brickSize * (y + brickSize * z);
}

[[nodiscard]] inline bool isFilled(const std::vector<std::uint64_t>& occupancy, std::uint32_t index)
{
    return ((occupancy[index >> 6] >> (index & 63)) & 1ull) != 0;
}

inline void setFilled(std::vector<std::uint64_t>& occupancy, std::uint32_t index)
{
    occupancy[index >> 6] |= 1ull << (index & 63);
}

[[nodiscard]] inline std::uint32_t filledCount(const std::vector<std::uint64_t>& occupancy)
{
    std::uint32_t count = 0;
    for (const std::uint64_t word : occupancy) {
        count += static_cast<std::uint32_t>(std::popcount(word));
    }
    return count;
}

} // namespace thesis::voxel
