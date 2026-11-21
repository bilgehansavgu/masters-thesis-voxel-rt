#pragma once

// Side: CPU

#include "thesis/core/GpuTypes.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace thesis::tracers {

// One node of the BVH: 32 bytes, so two nodes fill one 64-byte line of GPU memory.
// Mirrored by BvhNode in trace_software_bvh.slang; tests/TestShaderLayouts.cpp compares them.
//
// An inner node has two children. They are the nodes `child` and `child + 1`, next to each
// other in memory, and `primitive` is kNoBvhPrimitive. A leaf holds one primitive: `primitive`
// is its index, and `child` is not used.
struct BvhNode {
    glm::vec3 lo; // the box around everything below this node
    std::uint32_t child;
    glm::vec3 hi;
    std::uint32_t primitive;
};
static_assert(sizeof(BvhNode) == 32);

inline constexpr std::uint32_t kNoBvhPrimitive = 0xFFFFFFFFu;

// The search structure of the BVH tracer: our own bounding volume hierarchy, a binary tree of
// boxes. Node 0 is the root. trace_software_bvh.slang walks it with a small stack.
struct Bvh {
    std::vector<BvhNode> nodes;
    std::uint32_t depth = 0; // nodes on the longest way from the root to a leaf
};

// Builds the BVH from the boxes of the primitives alone: the same input that the GPU gets for
// its own BVH.
//
// The tree is built from the top. A set of primitives is split in two where the surface area
// heuristic (SAH) gives the lowest cost: the area of each half times the number of primitives
// in it. The cost is tried at 16 positions along each axis (binned SAH, Wald 2007). A set that
// cannot be split that way (all centres at one point) is cut in the middle of its list.
// Every leaf holds one primitive.
[[nodiscard]] Bvh buildBvh(std::span<const core::Aabb> boxes);

} // namespace thesis::tracers
