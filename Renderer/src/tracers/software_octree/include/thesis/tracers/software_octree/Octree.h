#pragma once

// Side: CPU

#include "thesis/core/GpuTypes.h"
#include "thesis/tracing/GridFrame.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace thesis::tracers {

// The search structure of the octree tracer: a sparse octree over the cells of a grid frame.
//
// The frame is extended to a cube of 2^depth cells along each side. A node covers a cube of
// cells. The root covers all of them. A node with content has eight children, one for each
// half along x, y and z. A part of space without any primitive is one EMPTY word, however
// large it is, so a ray crosses empty space in a few large steps. This is what the uniform
// grid cannot do.
//
// The words of `nodes` and `items` are described in OctreeShared.h.
// trace_software_octree.slang walks the tree.
struct Octree {
    glm::vec3 origin{0.0f}; // corner of cell (0, 0, 0)
    glm::vec3 cellSize{1.0f};
    std::uint32_t depth = 1;          // the cube has 2^depth cells along each side
    std::vector<std::uint32_t> nodes; // tiles of 8 words; tile 0 holds the children of the root
    std::vector<std::uint32_t> items; // the lists of the cells with more than one primitive
};

// Builds the octree. A primitive belongs to the cells that frame.forEachCellOf gives, the same
// cells as in the uniform grid. Only cells with a primitive get nodes, so the cost and the
// memory follow the number of primitives, not the volume of the scene.
[[nodiscard]] Octree buildOctree(std::span<const core::Aabb> boxes, const tracing::GridFrame& frame);

// What a walk from the root finds for one cell: the primitives of the cell, and the size of
// the cube that the walk ended in (cells along one side). For tests.
struct OctreeLookup {
    std::vector<std::uint32_t> primitives;
    std::uint32_t cubeSize = 1;
};
[[nodiscard]] OctreeLookup lookUp(const Octree& octree, const glm::uvec3& cell);

} // namespace thesis::tracers
