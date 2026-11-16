#pragma once

#include <glm/vec3.hpp>

#include <cstdint>

namespace thesis::brick2012 {

// CPU mirrors of the structs in shaders/common/brick2012.slang. The GPU reads them with std430
// layout. tests/TestShaderLayouts.cpp checks the shader side against them field by field.

// Buffer "header": facts about the whole model. 3 x 16 bytes.
struct BrickModelHeader {
    glm::vec3 origin; // corner of voxel (0, 0, 0) of the whole grid
    float voxelSize;
    glm::uvec3 gridSize; // voxels along each axis
    std::uint32_t brickSize;
    std::uint32_t brickCount;
    std::uint32_t layout; // THESIS_BRICK_LAYOUT_*
    std::uint32_t reserved0;
    std::uint32_t reserved1;
};
static_assert(sizeof(BrickModelHeader) == 48);

// Buffer "brick table": one record per brick, at the index of its primitive. A tracer reaches
// the data of a brick only through this record, which later lets bricks be loaded on demand.
struct BrickRecord {
    std::uint32_t position;        // brick coordinate and primary axis, see THESIS_BRICK_*
    std::uint32_t cellBase;        // index of the brick's first cell in the cell buffer
    std::uint32_t indirectionBase; // index of the brick's first word in the indirection buffer
    std::uint32_t attributeBase;   // split layout: index of the brick's first normal
};
static_assert(sizeof(BrickRecord) == 16);

} // namespace thesis::brick2012
