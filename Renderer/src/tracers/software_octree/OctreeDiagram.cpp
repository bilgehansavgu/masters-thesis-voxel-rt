// What the window shows of the sparse octree: a cut through its cubes, and the nodes a ray reads.
// Built with the same frame and builder as the octree of the tracer.

#include "OctreeDiagram.h"

#include "thesis/shared/RayLog.h"
#include "thesis/tracers/software_octree/Octree.h"
#include "thesis/tracers/software_octree/OctreeShared.h"
#include "thesis/tracing/GridFrame.h"

#include <glm/common.hpp>

#include <format>
#include <vector>

namespace thesis::tracers {
namespace {

class OctreeDiagram final : public tracing::IStructureDiagram {
public:
    explicit OctreeDiagram(const tracing::DiagramInput& input)
        : frame_(tracing::chooseGridFrame(input.primitiveBounds.size(), input.bounds, input.primitiveGrid)),
          octree_(buildOctree(input.primitiveBounds, frame_))
    {}

    tracing::StructureCut cut(float y, int /*detail*/) const override
    {
        tracing::StructureCut cut;
        const std::uint32_t layer = frame_.cellOf({frame_.origin.x, y, frame_.origin.z}).y;
        std::size_t full = 0, empty = 0;
        cutTile(0, glm::uvec3(0), 1u << (octree_.depth - 1), layer, cut, full, empty);
        const std::uint64_t gridCells = static_cast<std::uint64_t>(frame_.size.x) * frame_.size.z;
        cut.text = std::format("A horizontal cut through the octree at this height. It covers the cut with {} "
                               "squares: {} hold a brick, {} are empty space of different sizes. The uniform grid "
                               "needs {} cells for the same cut. A ray crosses each empty square in one step.",
                               full + empty, full, empty, gridCells);
        cut.legend = {{tracing::CutRect::Kind::Full, "Cell with a brick"},
                      {tracing::CutRect::Kind::Empty, "Empty cube (not divided further)"}};
        return cut;
    }

    tracing::WalkText walkText() const override
    {
        return {"node reads", "brick test",
                "Sparse octree: each node splits its cube into 8 children; empty cubes are not split. The walk reads "
                "nodes from the root down to the node that holds the current cell, then jumps to the far side of "
                "that node's cube. An empty node of 2^L cells is crossed in one step."};
    }

    void beginWalk() override { steps_ = 0; }

    std::optional<tracing::WalkStep> walkStep(std::span<const core::RayLogEntry> steps, std::size_t i,
                                              glm::vec3 /*origin*/, glm::vec3 /*direction*/, float /*closest*/) override
    {
        const core::RayLogEntry& e = steps[i];
        if (e.kind != THESIS_LOG_STRUCTURE_STEP)
            return std::nullopt;
        ++steps_;
        tracing::WalkStep step;
        const std::uint32_t side = 1u << e.d;
        step.boxes.push_back(cube(glm::uvec3(e.a, e.b, e.c) / side * side, side));
        const auto [next, j] = tracing::nextStructureEvent(steps, i);
        const bool descend = next == THESIS_LOG_STRUCTURE_STEP && j < steps.size() && steps[j].d < e.d;
        const char* action = descend                        ? "interior -> descend"
                             : next == THESIS_LOG_PRIMITIVE ? "leaf -> brick test"
                                                            : "empty -> skip";
        step.positive = next == THESIS_LOG_PRIMITIVE;
        step.label = std::format("node read {} · level {} ({}^3 cells) · {}", steps_, e.d, side, action);
        return step;
    }

private:
    [[nodiscard]] core::Aabb cube(glm::uvec3 cell, std::uint32_t side) const
    {
        const glm::vec3 lo = frame_.origin + glm::vec3(cell) * frame_.cellSize;
        return {lo, lo + static_cast<float>(side) * frame_.cellSize};
    }

    void cutTile(std::uint32_t tile, glm::uvec3 cell, std::uint32_t childSide, std::uint32_t layer,
                 tracing::StructureCut& cut, std::size_t& full, std::size_t& empty) const
    {
        for (std::uint32_t i = 0; i < 8; ++i) {
            const glm::uvec3 child = cell + glm::uvec3(i & 1u, (i >> 1) & 1u, (i >> 2) & 1u) * childSide;
            if (layer < child.y || layer >= child.y + childSide)
                continue;
            // The octree is a cube of 2^depth cells; the grid may be smaller. Show only inside the grid.
            if (child.x >= frame_.size.x || child.z >= frame_.size.z)
                continue;
            const std::uint32_t word = octree_.nodes.at(static_cast<std::size_t>(tile) * 8 + i);
            const std::uint32_t kind = word >> THESIS_OCTREE_KIND_SHIFT;
            if (kind == THESIS_OCTREE_INTERIOR) {
                cutTile(word & THESIS_OCTREE_PAYLOAD_MASK, child, childSide / 2, layer, cut, full, empty);
                continue;
            }
            const glm::uvec3 last =
                glm::min(child + childSide, glm::uvec3(frame_.size.x, child.y + childSide, frame_.size.z));
            const glm::vec3 lo = frame_.origin + glm::vec3(child) * frame_.cellSize;
            const glm::vec3 hi = frame_.origin + glm::vec3(last) * frame_.cellSize;
            const bool isEmpty = kind == THESIS_OCTREE_EMPTY;
            (isEmpty ? empty : full) += 1;
            cut.rects.push_back(
                {{lo.x, lo.z}, {hi.x, hi.z}, isEmpty ? tracing::CutRect::Kind::Empty : tracing::CutRect::Kind::Full});
        }
    }

    tracing::GridFrame frame_;
    Octree octree_;
    std::size_t steps_ = 0;
};

} // namespace

std::unique_ptr<tracing::IStructureDiagram> makeOctreeDiagram(const tracing::DiagramInput& input)
{
    return std::make_unique<OctreeDiagram>(input);
}

} // namespace thesis::tracers
