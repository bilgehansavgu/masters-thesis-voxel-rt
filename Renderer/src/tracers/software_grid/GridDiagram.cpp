// What the window shows of the uniform grid: a cut through its cells, and the cells a ray steps
// through. Built with the same frame as the grid of the tracer (tracing::chooseGridFrame).

#include "GridDiagram.h"

#include "thesis/shared/RayLog.h"
#include "thesis/tracers/software_grid/UniformGrid.h"

#include <format>
#include <set>
#include <utility>
#include <vector>

namespace thesis::tracers {
namespace {

class GridDiagram final : public tracing::IStructureDiagram {
public:
    explicit GridDiagram(const tracing::DiagramInput& input)
        : boxes_(input.primitiveBounds.begin(), input.primitiveBounds.end()),
          frame_(tracing::chooseGridFrame(boxes_.size(), input.bounds, input.primitiveGrid))
    {}

    tracing::StructureCut cut(float y, int /*detail*/) const override
    {
        tracing::StructureCut cut;
        const std::uint32_t layer = frame_.cellOf({frame_.origin.x, y, frame_.origin.z}).y;
        std::set<std::pair<std::uint32_t, std::uint32_t>> cells;
        for (const auto& box : boxes_) {
            frame_.forEachCellOf(box, [&](std::uint32_t x, std::uint32_t cellY, std::uint32_t z) {
                if (cellY == layer)
                    cells.insert({x, z});
            });
        }
        for (const auto& [x, z] : cells) {
            const glm::vec3 lo = frame_.origin + glm::vec3(x, layer, z) * frame_.cellSize;
            const glm::vec3 hi = lo + frame_.cellSize;
            cut.rects.push_back({{lo.x, lo.z}, {hi.x, hi.z}, tracing::CutRect::Kind::Full});
        }
        cut.gridLines = tracing::CutGridLines{
            {frame_.origin.x, frame_.origin.z}, {frame_.cellSize.x, frame_.cellSize.z}, {frame_.size.x, frame_.size.z}};
        const std::uint64_t cellsInCut = static_cast<std::uint64_t>(frame_.size.x) * frame_.size.z;
        cut.text = std::format("A horizontal cut through the grid at this height. This cut crosses {} cells. {} of "
                               "them hold a brick. The ray steps through every cell on its path, also the empty "
                               "ones.",
                               cellsInCut, cells.size());
        cut.legend = {{tracing::CutRect::Kind::Full, "Cell with a brick"},
                      {tracing::CutRect::Kind::Line, "Empty cell"}};
        return cut;
    }

    tracing::WalkText walkText() const override
    {
        return {"cells", "brick test",
                "Uniform grid: equal cells, one per brick. A 3D DDA visits every cell that the ray crosses, in "
                "order, empty cells included. A cell that holds a brick runs the brick test."};
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
        const glm::vec3 lo = frame_.origin + glm::vec3(e.a, e.b, e.c) * frame_.cellSize;
        step.boxes.push_back({lo, lo + frame_.cellSize});
        step.positive = tracing::nextStructureEvent(steps, i).first == THESIS_LOG_PRIMITIVE;
        step.label = std::format("DDA step {} · cell ({}, {}, {}) · {}", steps_, e.a, e.b, e.c,
                                 step.positive ? "holds a brick" : "empty");
        return step;
    }

private:
    std::vector<core::Aabb> boxes_;
    tracing::GridFrame frame_;
    std::size_t steps_ = 0;
};

} // namespace

std::unique_ptr<tracing::IStructureDiagram> makeGridDiagram(const tracing::DiagramInput& input)
{
    return std::make_unique<GridDiagram>(input);
}

} // namespace thesis::tracers
