// Tests for the search structure of the software tracer, the uniform grid. No GPU needed.

#include "thesis/core/BoxField.h"
#include "thesis/tracers/software/UniformGrid.h"

#include <catch2/catch_test_macros.hpp>
#include <glm/glm.hpp>

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

using namespace thesis;

namespace {

std::span<const std::uint32_t> itemsOf(const tracers::UniformGrid& grid, std::uint32_t x, std::uint32_t y,
                                       std::uint32_t z)
{
    const std::size_t cell = x + grid.size.x * (y + static_cast<std::size_t>(grid.size.y) * z);
    return std::span(grid.items).subspan(grid.cellStart[cell], grid.cellStart[cell + 1] - grid.cellStart[cell]);
}

bool lists(const tracers::UniformGrid& grid, std::uint32_t x, std::uint32_t y, std::uint32_t z, std::uint32_t primitive)
{
    const std::span<const std::uint32_t> items = itemsOf(grid, x, y, z);
    return std::find(items.begin(), items.end(), primitive) != items.end();
}

} // namespace

TEST_CASE("grid without a hint lists a primitive in every cell that its box touches", "[grid]")
{
    const core::Scene scene = core::makeBoxField({});
    const tracers::UniformGrid grid = tracers::buildUniformGrid(scene.primitiveBounds, scene.bounds, std::nullopt);

    REQUIRE(grid.cellStart.size() == grid.cellCount() + 1);
    CHECK(grid.cellStart.front() == 0);
    CHECK(grid.cellStart.back() == grid.items.size());
    CHECK(std::is_sorted(grid.cellStart.begin(), grid.cellStart.end()));
    CHECK(grid.cellCount() > 1); // more than one cell, or the grid would speed up nothing

    // The whole scene is inside the grid.
    const glm::vec3 gridHi = grid.origin + grid.cellSize * glm::vec3(grid.size);
    CHECK(glm::all(glm::lessThanEqual(grid.origin, scene.bounds.lo)));
    CHECK(glm::all(glm::greaterThanEqual(gridHi, scene.bounds.hi)));

    // Every cell that overlaps a box lists that box. The cell is tested as a closed box.
    std::size_t missing = 0;
    for (std::uint32_t primitive = 0; primitive < scene.primitiveBounds.size(); ++primitive) {
        const core::Aabb& box = scene.primitiveBounds[primitive];
        for (std::uint32_t z = 0; z < grid.size.z; ++z) {
            for (std::uint32_t y = 0; y < grid.size.y; ++y) {
                for (std::uint32_t x = 0; x < grid.size.x; ++x) {
                    const glm::vec3 lo = grid.origin + grid.cellSize * glm::vec3(x, y, z);
                    const glm::vec3 hi = lo + grid.cellSize;
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

TEST_CASE("grid with a hint is the hint and lists each primitive in one cell", "[grid]")
{
    // Three boxes in a grid of 4 x 2 x 3 cells of size 0.5 that starts at (1, 2, 3).
    const core::RegularGrid hint{{1.0f, 2.0f, 3.0f}, 0.5f, {4, 2, 3}};
    const std::vector<core::Aabb> boxes{
        core::Aabb{{1.0f, 2.0f, 3.0f}, {1.5f, 2.5f, 3.5f}},    // fills cell (0, 0, 0): its sides are on the borders
        core::Aabb{{2.6f, 2.6f, 4.1f}, {2.9f, 2.9f, 4.4f}},    // inside cell (3, 1, 2)
        core::Aabb{{2.0f, 2.25f, 3.5f}, {2.5f, 2.5f, 3.75f}}}; // inside cell (2, 0, 1), touches three borders
    core::Bounds bounds;
    for (const core::Aabb& box : boxes) {
        bounds.extend(box);
    }

    const tracers::UniformGrid grid = tracers::buildUniformGrid(boxes, bounds, hint);
    CHECK(grid.origin == hint.origin);
    CHECK(grid.cellSize == glm::vec3(0.5f));
    CHECK(grid.size == hint.size);
    CHECK(grid.items.size() == boxes.size());
    CHECK(lists(grid, 0, 0, 0, 0));
    CHECK(lists(grid, 3, 1, 2, 1));
    CHECK(lists(grid, 2, 0, 1, 2));
}

TEST_CASE("grid refuses a size that is too large", "[grid]")
{
    const core::RegularGrid hint{{0.0f, 0.0f, 0.0f}, 1.0f, {1024, 1024, 1024}};
    const std::vector<core::Aabb> boxes{core::Aabb{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}};
    core::Bounds bounds;
    bounds.extend(boxes.front());
    CHECK_THROWS(tracers::buildUniformGrid(boxes, bounds, hint));
}
