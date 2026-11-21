// Tests for the search structures that the software tracers build on the CPU: the uniform
// grid, the octree and the BVH. No GPU needed. The tests of the tracers themselves (do they
// find the right hits?) are TestAgreement.cpp and TestBrickTracing.cpp.

#include "thesis/core/BoxField.h"
#include "thesis/tracers/software_bvh/Bvh.h"
#include "thesis/tracers/software_grid/UniformGrid.h"
#include "thesis/tracers/software_octree/Octree.h"
#include "thesis/tracing/GridFrame.h"

#include <catch2/catch_test_macros.hpp>
#include <glm/glm.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

using namespace thesis;

namespace {

std::span<const std::uint32_t> itemsOf(const tracers::UniformGrid& grid, std::uint32_t x, std::uint32_t y,
                                       std::uint32_t z)
{
    const glm::uvec3& size = grid.frame.size;
    const std::size_t cell = x + size.x * (y + static_cast<std::size_t>(size.y) * z);
    return std::span(grid.items).subspan(grid.cellStart[cell], grid.cellStart[cell + 1] - grid.cellStart[cell]);
}

bool lists(const tracers::UniformGrid& grid, std::uint32_t x, std::uint32_t y, std::uint32_t z, std::uint32_t primitive)
{
    const std::span<const std::uint32_t> items = itemsOf(grid, x, y, z);
    return std::find(items.begin(), items.end(), primitive) != items.end();
}

core::Bounds boundsOf(std::span<const core::Aabb> boxes)
{
    core::Bounds bounds;
    for (const core::Aabb& box : boxes) {
        bounds.extend(box);
    }
    return bounds;
}

// Three boxes in a grid of 4 x 2 x 3 cells of size 0.5 that starts at (1, 2, 3).
const core::RegularGrid kHint{{1.0f, 2.0f, 3.0f}, 0.5f, {4, 2, 3}};
const std::vector<core::Aabb> kHintBoxes{
    core::Aabb{{1.0f, 2.0f, 3.0f}, {1.5f, 2.5f, 3.5f}},    // fills cell (0, 0, 0): its sides are on the borders
    core::Aabb{{2.6f, 2.6f, 4.1f}, {2.9f, 2.9f, 4.4f}},    // inside cell (3, 1, 2)
    core::Aabb{{2.0f, 2.25f, 3.5f}, {2.5f, 2.5f, 3.75f}}}; // inside cell (2, 0, 1), touches three borders

// The octree must give, for every cell of its cube, the primitives that the uniform grid lists.
void checkOctreeAgainstGrid(std::span<const core::Aabb> boxes, const tracing::GridFrame& frame)
{
    const tracers::UniformGrid grid = tracers::buildUniformGrid(boxes, frame);
    const tracers::Octree octree = tracers::buildOctree(boxes, frame);
    REQUIRE(octree.nodes.size() % 8 == 0);
    const std::uint32_t side = 1u << octree.depth;
    REQUIRE(side >= std::max({frame.size.x, frame.size.y, frame.size.z}));
    REQUIRE(side < 2 * std::max({frame.size.x, frame.size.y, frame.size.z, 2u}));

    std::size_t wrong = 0;
    for (std::uint32_t z = 0; z < side; ++z) {
        for (std::uint32_t y = 0; y < side; ++y) {
            for (std::uint32_t x = 0; x < side; ++x) {
                const tracers::OctreeLookup found = tracers::lookUp(octree, {x, y, z});
                const bool inFrame = x < frame.size.x && y < frame.size.y && z < frame.size.z;
                const std::span<const std::uint32_t> expected =
                    inFrame ? itemsOf(grid, x, y, z) : std::span<const std::uint32_t>();
                if (!std::equal(found.primitives.begin(), found.primitives.end(), expected.begin(), expected.end())) {
                    ++wrong;
                }
                // Primitives are stored only in nodes of one cell.
                if (!found.primitives.empty() && found.cubeSize != 1) {
                    ++wrong;
                }
            }
        }
    }
    CHECK(wrong == 0);
}

} // namespace

TEST_CASE("grid without a hint lists a primitive in every cell that its box touches", "[structures][grid]")
{
    const core::Scene scene = core::makeBoxField({});
    const tracing::GridFrame frame = tracing::chooseGridFrame(scene.primitiveBounds.size(), scene.bounds, std::nullopt);
    const tracers::UniformGrid grid = tracers::buildUniformGrid(scene.primitiveBounds, frame);

    REQUIRE(grid.cellStart.size() == frame.cellCount() + 1);
    CHECK(grid.cellStart.front() == 0);
    CHECK(grid.cellStart.back() == grid.items.size());
    CHECK(std::is_sorted(grid.cellStart.begin(), grid.cellStart.end()));
    CHECK(frame.cellCount() > 1); // more than one cell, or the grid would speed up nothing
    CHECK_FALSE(frame.oneCellEach);

    // The whole scene is inside the grid.
    const glm::vec3 gridHi = frame.origin + frame.cellSize * glm::vec3(frame.size);
    CHECK(glm::all(glm::lessThanEqual(frame.origin, scene.bounds.lo)));
    CHECK(glm::all(glm::greaterThanEqual(gridHi, scene.bounds.hi)));

    // Every cell that overlaps a box lists that box.
    std::size_t missing = 0;
    for (std::uint32_t primitive = 0; primitive < scene.primitiveBounds.size(); ++primitive) {
        const core::Aabb& box = scene.primitiveBounds[primitive];
        for (std::uint32_t z = 0; z < frame.size.z; ++z) {
            for (std::uint32_t y = 0; y < frame.size.y; ++y) {
                for (std::uint32_t x = 0; x < frame.size.x; ++x) {
                    const glm::vec3 lo = frame.origin + frame.cellSize * glm::vec3(x, y, z);
                    const glm::vec3 hi = lo + frame.cellSize;
                    const bool overlaps = glm::all(glm::lessThan(box.lo, hi)) && glm::all(glm::greaterThan(box.hi, lo));
                    if (overlaps && !lists(grid, x, y, z, primitive)) {
                        ++missing;
                    }
                }
            }
        }
    }
    CHECK(missing == 0);
}

TEST_CASE("grid with a hint is the hint and lists each primitive in one cell", "[structures][grid]")
{
    const tracing::GridFrame frame = tracing::chooseGridFrame(kHintBoxes.size(), boundsOf(kHintBoxes), kHint);
    CHECK(frame.origin == kHint.origin);
    CHECK(frame.cellSize == glm::vec3(0.5f));
    CHECK(frame.size == kHint.size);
    CHECK(frame.oneCellEach);

    const tracers::UniformGrid grid = tracers::buildUniformGrid(kHintBoxes, frame);
    CHECK(grid.items.size() == kHintBoxes.size());
    CHECK(lists(grid, 0, 0, 0, 0));
    CHECK(lists(grid, 3, 1, 2, 1));
    CHECK(lists(grid, 2, 0, 1, 2));
}

TEST_CASE("grid refuses a size that is too large", "[structures][grid]")
{
    const std::vector<core::Aabb> boxes{core::Aabb{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}};
    // More cells along one axis than any frame may have.
    CHECK_THROWS(
        tracing::chooseGridFrame(1, boundsOf(boxes), core::RegularGrid{{0.0f, 0.0f, 0.0f}, 1.0f, {2048, 1, 1}}));
    // A frame that is allowed, but too many cells for a grid that stores every cell.
    const tracing::GridFrame large =
        tracing::chooseGridFrame(1, boundsOf(boxes), core::RegularGrid{{0.0f, 0.0f, 0.0f}, 1.0f, {1024, 1024, 1024}});
    CHECK_THROWS(tracers::buildUniformGrid(boxes, large));
}

TEST_CASE("octree gives the primitives of the grid for every cell", "[structures][octree]")
{
    SECTION("scene without a hint: a primitive can be in many cells")
    {
        const core::Scene scene = core::makeBoxField({.gridSize = 8});
        checkOctreeAgainstGrid(scene.primitiveBounds,
                               tracing::chooseGridFrame(scene.primitiveBounds.size(), scene.bounds, std::nullopt));
    }
    SECTION("scene with a hint: one cell for each primitive")
    {
        checkOctreeAgainstGrid(kHintBoxes, tracing::chooseGridFrame(kHintBoxes.size(), boundsOf(kHintBoxes), kHint));
    }
    SECTION("several primitives in one cell")
    {
        // Two boxes with their centres in cell (1, 0, 0) of the hint grid, one in cell (0, 0, 0).
        const std::vector<core::Aabb> boxes{core::Aabb{{1.6f, 2.1f, 3.1f}, {1.7f, 2.2f, 3.2f}},
                                            core::Aabb{{1.1f, 2.1f, 3.1f}, {1.2f, 2.2f, 3.2f}},
                                            core::Aabb{{1.8f, 2.3f, 3.3f}, {1.9f, 2.4f, 3.4f}}};
        const tracing::GridFrame frame = tracing::chooseGridFrame(boxes.size(), boundsOf(boxes), kHint);
        checkOctreeAgainstGrid(boxes, frame);
        const tracers::Octree octree = tracers::buildOctree(boxes, frame);
        CHECK(tracers::lookUp(octree, {1, 0, 0}).primitives == std::vector<std::uint32_t>{0, 2});
        CHECK(tracers::lookUp(octree, {0, 0, 0}).primitives == std::vector<std::uint32_t>{1});
    }
}

TEST_CASE("octree stores a large and mostly empty grid in little memory", "[structures][octree]")
{
    // A frame of 1024 cells along each axis: the uniform grid refuses it (previous test).
    const core::RegularGrid hint{{0.0f, 0.0f, 0.0f}, 1.0f, {1024, 1024, 1024}};
    const std::vector<core::Aabb> boxes{core::Aabb{{0.2f, 0.2f, 0.2f}, {0.8f, 0.8f, 0.8f}},
                                        core::Aabb{{1023.2f, 1023.2f, 1023.2f}, {1023.8f, 1023.8f, 1023.8f}},
                                        core::Aabb{{511.2f, 3.2f, 700.2f}, {511.8f, 3.8f, 700.8f}}};
    const tracers::Octree octree =
        tracers::buildOctree(boxes, tracing::chooseGridFrame(boxes.size(), boundsOf(boxes), hint));
    CHECK(octree.depth == 10);
    CHECK(octree.nodes.size() <= 8 * 10 * boxes.size()); // at most one tile for each level and primitive
    CHECK(tracers::lookUp(octree, {0, 0, 0}).primitives == std::vector<std::uint32_t>{0});
    CHECK(tracers::lookUp(octree, {1023, 1023, 1023}).primitives == std::vector<std::uint32_t>{1});
    CHECK(tracers::lookUp(octree, {511, 3, 700}).primitives == std::vector<std::uint32_t>{2});

    // A cell far from every primitive is inside a large empty cube.
    const tracers::OctreeLookup empty = tracers::lookUp(octree, {900, 100, 100});
    CHECK(empty.primitives.empty());
    CHECK(empty.cubeSize >= 256);
}

TEST_CASE("BVH holds every primitive once, inside the boxes of its nodes", "[structures][bvh]")
{
    const core::Scene scene = core::makeBoxField({});
    const std::vector<core::Aabb>& boxes = scene.primitiveBounds;
    const tracers::Bvh bvh = tracers::buildBvh(boxes);

    // A binary tree with one primitive in each leaf has 2n - 1 nodes.
    REQUIRE(bvh.nodes.size() == 2 * boxes.size() - 1);
    CHECK(bvh.depth >= 2);
    CHECK(bvh.depth <= 64);

    const auto contains = [](const tracers::BvhNode& outer, const glm::vec3& lo, const glm::vec3& hi) {
        return glm::all(glm::lessThanEqual(outer.lo, lo)) && glm::all(glm::greaterThanEqual(outer.hi, hi));
    };
    std::vector<std::uint32_t> seen(boxes.size(), 0);
    std::vector<std::uint32_t> reached(bvh.nodes.size(), 0);
    std::size_t wrong = 0;
    std::vector<std::uint32_t> stack{0};
    reached[0] = 1;
    while (!stack.empty()) {
        const tracers::BvhNode& node = bvh.nodes[stack.back()];
        stack.pop_back();
        if (node.primitive != tracers::kNoBvhPrimitive) {
            REQUIRE(node.primitive < boxes.size());
            ++seen[node.primitive];
            wrong += contains(node, boxes[node.primitive].lo, boxes[node.primitive].hi) ? 0 : 1;
            continue;
        }
        REQUIRE(node.child + 1 < bvh.nodes.size());
        for (std::uint32_t child = node.child; child <= node.child + 1; ++child) {
            wrong += contains(node, bvh.nodes[child].lo, bvh.nodes[child].hi) ? 0 : 1;
            ++reached[child];
            stack.push_back(child);
        }
    }
    CHECK(wrong == 0);
    CHECK(std::all_of(seen.begin(), seen.end(), [](std::uint32_t count) { return count == 1; }));
    CHECK(std::all_of(reached.begin(), reached.end(), [](std::uint32_t count) { return count == 1; }));
}

TEST_CASE("BVH handles one primitive and primitives at the same place", "[structures][bvh]")
{
    const core::Aabb box{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}};

    const tracers::Bvh single = tracers::buildBvh(std::vector<core::Aabb>{box});
    REQUIRE(single.nodes.size() == 1);
    CHECK(single.nodes[0].primitive == 0);
    CHECK(single.depth == 1);

    // 100 boxes with the same centre: no position separates them, so the builder cuts the list
    // in the middle. The tree must stay shallow.
    const tracers::Bvh same = tracers::buildBvh(std::vector<core::Aabb>(100, box));
    CHECK(same.nodes.size() == 199);
    CHECK(same.depth <= 8);
}
