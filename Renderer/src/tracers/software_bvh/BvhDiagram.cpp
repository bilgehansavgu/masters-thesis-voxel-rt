// What the window shows of our BVH: a cut through its boxes at one tree level, and the boxes a ray
// tests. Built with the same builder as the BVH of the tracer.

#include "BvhDiagram.h"

#include "thesis/core/RayBox.h"
#include "thesis/shared/RayLog.h"
#include "thesis/tracers/software_bvh/Bvh.h"

#include <algorithm>
#include <format>
#include <set>
#include <vector>

namespace thesis::tracers {
namespace {

class BvhDiagram final : public tracing::IStructureDiagram {
public:
    explicit BvhDiagram(const tracing::DiagramInput& input) : bvh_(buildBvh(input.primitiveBounds))
    {
        // The depth of each node: the children of a node come after it in the node list.
        depth_.assign(bvh_.nodes.size(), 0);
        for (std::size_t i = 0; i < bvh_.nodes.size(); ++i) {
            const BvhNode& node = bvh_.nodes[i];
            if (node.primitive == kNoBvhPrimitive) {
                depth_.at(node.child) = depth_[i] + 1;
                depth_.at(node.child + 1) = depth_[i] + 1;
            }
        }
    }

    std::optional<tracing::DiagramDetail> detail() const override
    {
        const int deepest = static_cast<int>(std::max<std::uint32_t>(bvh_.depth, 2)) - 1;
        return tracing::DiagramDetail{"Tree level", 1, deepest, std::min(4, deepest)};
    }

    tracing::StructureCut cut(float y, int level) const override
    {
        tracing::StructureCut cut;
        std::size_t leaves = 0, groups = 0;
        cutNode(0, 0, y, static_cast<std::uint32_t>(level), cut, leaves, groups);
        cut.text = std::format("A horizontal cut through the boxes of the tree at level {}. {} boxes cross the cut. "
                               "Each box encloses a group of bricks. Boxes can overlap. A ray that misses a box skips "
                               "everything inside it. Raise the level to see smaller groups.",
                               level, groups + leaves);
        cut.legend = {{tracing::CutRect::Kind::Group, "Box of a group of bricks"},
                      {tracing::CutRect::Kind::Full, "Box of a single brick (a leaf above this level)"}};
        return cut;
    }

    tracing::WalkText walkText() const override
    {
        return {"node AABB tests", "brick test",
                "Software BVH: a binary tree of AABBs around groups of bricks. At an inner node the ray tests both "
                "child AABBs, enters the nearer one and pushes the other on a stack. A missed AABB culls its whole "
                "subtree. A leaf runs the brick test. Child AABBs can overlap."};
    }

    void beginWalk() override { seen_.clear(); }

    std::optional<tracing::WalkStep> walkStep(std::span<const core::RayLogEntry> steps, std::size_t i, glm::vec3 origin,
                                              glm::vec3 direction, float closest) override
    {
        const core::RayLogEntry& e = steps[i];
        if (e.kind != THESIS_LOG_STRUCTURE_STEP || e.a >= bvh_.nodes.size())
            return std::nullopt;
        tracing::WalkStep step;
        const BvhNode& node = bvh_.nodes[e.a];
        const core::Aabb box{node.lo, node.hi};
        step.boxes.push_back(box);
        const bool resumed = !seen_.insert(e.a).second;
        const auto inside = core::rayBoxInterval(origin, direction, box);
        const bool enters = inside && inside->x <= closest;
        step.positive = enters;
        const std::uint32_t depth = depth_[e.a];
        const char* kind = node.primitive == kNoBvhPrimitive ? "inner" : "leaf";
        if (resumed)
            step.label = std::format("node {} · depth {} · {} · pop from stack", e.a, depth, kind);
        else if (enters)
            step.label = std::format("node {} · depth {} · {} · AABB hit, t = {:.4g}", e.a, depth, kind, inside->x);
        else
            step.label = std::format("node {} · depth {} · {} · AABB miss -> subtree culled", e.a, depth, kind);
        if (enters)
            step.t = inside->x;
        return step;
    }

private:
    void cutNode(std::uint32_t index, std::uint32_t depth, float y, std::uint32_t level, tracing::StructureCut& cut,
                 std::size_t& leaves, std::size_t& groups) const
    {
        const BvhNode& node = bvh_.nodes.at(index);
        if (y < node.lo.y || y > node.hi.y)
            return;
        const bool leaf = node.primitive != kNoBvhPrimitive;
        if (leaf || depth == level) {
            (leaf ? leaves : groups) += 1;
            cut.rects.push_back({{node.lo.x, node.lo.z},
                                 {node.hi.x, node.hi.z},
                                 leaf ? tracing::CutRect::Kind::Full : tracing::CutRect::Kind::Group});
            return;
        }
        cutNode(node.child, depth + 1, y, level, cut, leaves, groups);
        cutNode(node.child + 1, depth + 1, y, level, cut, leaves, groups);
    }

    Bvh bvh_;
    std::vector<std::uint32_t> depth_; // depth of each node
    std::set<std::uint32_t> seen_;     // nodes the current ray has visited before
};

} // namespace

std::unique_ptr<tracing::IStructureDiagram> makeBvhDiagram(const tracing::DiagramInput& input)
{
    return std::make_unique<BvhDiagram>(input);
}

} // namespace thesis::tracers
