#pragma once

// ============================================================================
// READING ORDER: FILE 3A (The plain VoxelModel: 1 bit per voxel inside bricks)
// ============================================================================
// VOXEL BASICS: What is a Voxel and what is a Brick?
// Just as a 2D image is a grid of small squares called "pixels", a 3D volume grid is made of
// small cubes called "voxels" (volumetric pixels).
//
// If you have a 2048 x 2048 x 2048 grid, most of that 3D space is empty air!
// Instead of storing a giant 3D array with 8.5 billion entries, we chop the 3D grid into small
// cubes of `brickSize x brickSize x brickSize` voxels (typically 32 x 32 x 32 = 32,768 voxels)
// called **bricks**, and we ONLY store the bricks that contain at least one surface voxel.
//
// Inside each non-empty `VoxelBrick` (before any 2012 compression is applied):
//   1. `occupancy`: a bitset with 1 bit per voxel (for a 32^3 brick, 32,768 bits = 512 `uint64_t`
//      words). Bit `index` is `1` if that voxel is filled, `0` if empty air.
//   2. `normals`: a compact array of 16-bit octahedral normals (`OctNormal.h`), storing ONLY
//      the normals of the filled voxels (in ascending bit index order).

#include <glm/vec3.hpp>

#include <bit> // C++20 `<bit>` header for `std::popcount` (counts how many 1-bits are in a uint64_t)
#include <cstdint>
#include <string>
#include <vector>

namespace thesis::voxel {

// Where the voxels are in the world. Voxel (x, y, z) is the cube from
// origin + voxelSize * (x, y, z) to origin + voxelSize * (x + 1, y + 1, z + 1).
struct VoxelGrid {
    glm::vec3 origin{0.0f};
    float voxelSize = 1.0f;
    glm::uvec3 size{0};           // total voxels along each axis (X, Y, Z), always a multiple of brickSize
    std::uint32_t brickSize = 32; // voxels along one side of a brick: 8, 16 or 32

    // C++20 REFRESHER: `= default` on `operator==` tells the compiler to automatically compare
    // every struct member field-by-field for equality (`a == b`).
    bool operator==(const VoxelGrid&) const = default;
};

// One cell of the regular brick grid, with the voxels inside it.
//
// `occupancy` has one bit per voxel of the brick: bit x + N * (y + N * z) is set when voxel
// (x, y, z) of the brick is filled, with N = brickSize. `normals` has one entry per filled voxel,
// in ascending bit order: the surface normal, packed into 16 bits (see OctNormal.h).
struct VoxelBrick {
    glm::uvec3 coordinate{0}; // brick grid position: the first voxel of the brick is coordinate * brickSize
    std::vector<std::uint64_t> occupancy;
    std::vector<std::uint16_t> normals;

    bool operator==(const VoxelBrick&) const = default;
};

// How the model was made, kept for reports.
struct VoxelSource {
    std::string mesh;
    std::uint64_t triangles = 0;
    std::string rule;             // voxelization rule ("sampled" or "conservative"), see Voxelizer.h
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

// Flattens a 3D voxel position (x, y, z) inside a brick (each in 0 .. brickSize - 1) into a
// single 1D bit index in 0 .. brickSize^3 - 1.
[[nodiscard]] constexpr std::uint32_t voxelIndex(std::uint32_t brickSize, std::uint32_t x, std::uint32_t y,
                                                 std::uint32_t z)
{
    return x + brickSize * (y + brickSize * z);
}

// C++ BIT MANIPULATION REFRESHER:
// `occupancy` is an array of 64-bit unsigned integers (`std::uint64_t`).
// Given a bit `index` (for example, 0 .. 32,767):
//   - Which 64-bit word holds that bit? `index / 64`, written as `index >> 6` (since 2^6 = 64).
//   - Which bit inside that 64-bit word (0..63)? `index % 64`, written as `index & 63`.
//   - `1ull << (index & 63)` creates a 64-bit mask with a `1` only at that bit position.
[[nodiscard]] inline bool isFilled(const std::vector<std::uint64_t>& occupancy, std::uint32_t index)
{
    return ((occupancy[index >> 6] >> (index & 63)) & 1ull) != 0;
}

inline void setFilled(std::vector<std::uint64_t>& occupancy, std::uint32_t index)
{
    occupancy[index >> 6] |= 1ull << (index & 63);
}

// Counts the total number of filled voxels (1-bits) in `occupancy` using `std::popcount`
// (which compiles down to a single fast CPU instruction `POPCNT`).
[[nodiscard]] inline std::uint32_t filledCount(const std::vector<std::uint64_t>& occupancy)
{
    std::uint32_t count = 0;
    for (const std::uint64_t word : occupancy) {
        count += static_cast<std::uint32_t>(std::popcount(word));
    }
    return count;
}

} // namespace thesis::voxel
