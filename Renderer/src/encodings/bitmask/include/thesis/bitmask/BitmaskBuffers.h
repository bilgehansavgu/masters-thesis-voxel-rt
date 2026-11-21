#pragma once

// Side: CPU

#include <glm/vec3.hpp>

#include <cstdint>

namespace thesis::bitmask {

// CPU mirrors of the structs in shaders/primitives/bitmask/primitive.slang. The GPU reads them
// with std430 layout. tests/TestShaderLayouts.cpp checks the shader side against them field by
// field. The buffers are described in shared/include/thesis/shared/Bitmask.h.

// Buffer "header": facts about the whole model. 3 x 16 bytes.
struct BitmaskModelHeader {
    glm::vec3 origin; // corner of voxel (0, 0, 0) of the whole grid
    float voxelSize;
    glm::uvec3 gridSize; // voxels along each axis
    std::uint32_t brickSize;
    std::uint32_t brickCount;
    std::uint32_t voxelCount; // filled voxels of the whole model: the number of normals
    std::uint32_t reserved0;
    std::uint32_t reserved1;
};
static_assert(sizeof(BitmaskModelHeader) == 48);

// Buffer "brick table": one record per brick, at the index of its primitive. The masks and the
// row counts of brick b start at fixed places (Bitmask.h), so the record needs no field for them.
struct BitmaskBrickRecord {
    std::uint32_t position;   // brick coordinate, see THESIS_BITMASK_COORDINATE_*
    std::uint32_t normalBase; // index of the brick's first normal in the normal buffer
};
static_assert(sizeof(BitmaskBrickRecord) == 8);

} // namespace thesis::bitmask
