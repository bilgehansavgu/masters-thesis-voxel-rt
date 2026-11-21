#include "thesis/tracing/HiddenStructureDiagram.h"

#include <format>
#include <utility>

namespace thesis::tracing {

HiddenStructureDiagram::HiddenStructureDiagram(const DiagramInput& input, WalkText walkText)
    : boxes_(input.primitiveBounds.begin(), input.primitiveBounds.end()), walkText_(std::move(walkText))
{}

StructureCut HiddenStructureDiagram::cut(float y, int /*detail*/) const
{
    StructureCut cut;
    for (const core::Aabb& box : boxes_) {
        if (box.lo.y <= y && y <= box.hi.y)
            cut.rects.push_back({{box.lo.x, box.lo.z}, {box.hi.x, box.hi.z}, CutRect::Kind::Full});
    }
    cut.text = std::format("The {} brick boxes that cross this cut. The program gives the GPU only these boxes. "
                           "The GPU builds its own tree from them and does not show that tree.",
                           cut.rects.size());
    cut.legend = {{CutRect::Kind::Full, "Brick box given to the GPU"}};
    return cut;
}

std::optional<WalkStep> HiddenStructureDiagram::walkStep(std::span<const core::RayLogEntry> /*steps*/,
                                                         std::size_t /*i*/, glm::vec3 /*origin*/,
                                                         glm::vec3 /*direction*/, float /*closest*/)
{
    return std::nullopt;
}

} // namespace thesis::tracing
