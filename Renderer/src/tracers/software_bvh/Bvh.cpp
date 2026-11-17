#include "thesis/tracers/software_bvh/Bvh.h"

#include "thesis/shared/Bvh.h"

#include <glm/common.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace thesis::tracers {
namespace {

static_assert(kNoBvhPrimitive == THESIS_BVH_NO_PRIMITIVE);

constexpr int kBins = 16; // positions along an axis at which a split is tried

struct Box {
    glm::vec3 lo{std::numeric_limits<float>::max()};
    glm::vec3 hi{std::numeric_limits<float>::lowest()};

    void extend(const glm::vec3& point)
    {
        lo = glm::min(lo, point);
        hi = glm::max(hi, point);
    }
    void extend(const core::Aabb& box)
    {
        lo = glm::min(lo, box.lo);
        hi = glm::max(hi, box.hi);
    }
    void extend(const Box& box)
    {
        lo = glm::min(lo, box.lo);
        hi = glm::max(hi, box.hi);
    }
    // Half the surface area. Zero for a box that holds nothing.
    [[nodiscard]] float halfArea() const
    {
        const glm::vec3 size = hi - lo;
        return size.x < 0.0f ? 0.0f : size.x * size.y + size.y * size.z + size.z * size.x;
    }
};

struct Bin {
    Box box;
    std::uint32_t count = 0;
};

// The best place to cut a set of primitives in two.
struct Split {
    int axis = -1; // -1: no cut found (all centres at one point)
    int bin = 0;   // the primitives in the bins 0..bin go to the first half
    float cost = std::numeric_limits<float>::max();
};

// A set of primitives that still has to become a subtree: order[first .. first + count).
struct Task {
    std::uint32_t node;
    std::uint32_t first;
    std::uint32_t count;
    std::uint32_t depth;
};

} // namespace

Bvh buildBvh(std::span<const core::Aabb> boxes)
{
    if (boxes.empty()) {
        throw std::invalid_argument("buildBvh: the scene has no primitives");
    }
    if (boxes.size() >= kNoBvhPrimitive / 2) {
        throw std::length_error("buildBvh: too many primitives");
    }
    const auto count = static_cast<std::uint32_t>(boxes.size());

    std::vector<std::uint32_t> order(count); // primitive indices; each subtree owns one range of it
    std::iota(order.begin(), order.end(), 0u);
    std::vector<glm::vec3> centres(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        centres[i] = 0.5f * (boxes[i].lo + boxes[i].hi);
    }

    Bvh bvh;
    bvh.nodes.reserve(2 * static_cast<std::size_t>(count) - 1);
    bvh.nodes.push_back({});
    std::vector<Task> tasks{{0, 0, count, 1}};
    while (!tasks.empty()) {
        const Task task = tasks.back();
        tasks.pop_back();
        const std::span<std::uint32_t> range(order.data() + task.first, task.count);

        Box bounds;
        Box centreBounds;
        for (const std::uint32_t primitive : range) {
            bounds.extend(boxes[primitive]);
            centreBounds.extend(centres[primitive]);
        }
        bvh.nodes[task.node].lo = bounds.lo;
        bvh.nodes[task.node].hi = bounds.hi;
        bvh.depth = std::max(bvh.depth, task.depth);
        if (task.count == 1) {
            bvh.nodes[task.node].child = 0;
            bvh.nodes[task.node].primitive = range.front();
            continue;
        }

        // The bin of a centre along an axis.
        const glm::vec3 extent = centreBounds.hi - centreBounds.lo;
        const auto binOf = [&](std::uint32_t primitive, int axis) {
            const float position = (centres[primitive][axis] - centreBounds.lo[axis]) / extent[axis];
            return std::min(kBins - 1, static_cast<int>(position * static_cast<float>(kBins)));
        };

        // Binned SAH: for each axis, put the primitives into bins by their centre, then try a
        // cut after each bin. The cost of a cut is area x count of the first half plus area x
        // count of the second half.
        Split best;
        for (int axis = 0; axis < 3; ++axis) {
            if (!(extent[axis] > 0.0f)) {
                continue;
            }
            std::array<Bin, kBins> bins{};
            for (const std::uint32_t primitive : range) {
                Bin& bin = bins[static_cast<std::size_t>(binOf(primitive, axis))];
                bin.box.extend(boxes[primitive]);
                ++bin.count;
            }
            std::array<float, kBins> costOfSecondHalf{}; // of the bins i + 1 .. kBins - 1
            Box box;
            std::uint32_t boxCount = 0;
            for (int i = kBins - 1; i > 0; --i) {
                box.extend(bins[static_cast<std::size_t>(i)].box);
                boxCount += bins[static_cast<std::size_t>(i)].count;
                costOfSecondHalf[static_cast<std::size_t>(i - 1)] = box.halfArea() * static_cast<float>(boxCount);
            }
            box = Box{};
            boxCount = 0;
            for (int i = 0; i < kBins - 1; ++i) {
                box.extend(bins[static_cast<std::size_t>(i)].box);
                boxCount += bins[static_cast<std::size_t>(i)].count;
                if (boxCount == 0 || boxCount == task.count) {
                    continue; // one half would be empty
                }
                const float cost =
                    box.halfArea() * static_cast<float>(boxCount) + costOfSecondHalf[static_cast<std::size_t>(i)];
                if (cost < best.cost) {
                    best = {axis, i, cost};
                }
            }
        }

        std::uint32_t firstHalf = task.count / 2; // the cut in the middle, if no better one exists
        if (best.axis >= 0) {
            const auto middle = std::partition(range.begin(), range.end(), [&](std::uint32_t primitive) {
                return binOf(primitive, best.axis) <= best.bin;
            });
            firstHalf = static_cast<std::uint32_t>(middle - range.begin());
        }

        // The two children are next to each other in memory.
        const auto child = static_cast<std::uint32_t>(bvh.nodes.size());
        bvh.nodes.push_back({});
        bvh.nodes.push_back({});
        bvh.nodes[task.node].child = child;
        bvh.nodes[task.node].primitive = kNoBvhPrimitive;
        tasks.push_back({child + 1, task.first + firstHalf, task.count - firstHalf, task.depth + 1});
        tasks.push_back({child, task.first, firstHalf, task.depth + 1});
    }
    return bvh;
}

} // namespace thesis::tracers
