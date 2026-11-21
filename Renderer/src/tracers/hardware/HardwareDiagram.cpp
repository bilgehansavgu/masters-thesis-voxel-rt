#include "HardwareDiagram.h"

#include <format>
#include <vector>

namespace thesis::tracers {
namespace {

class HardwareDiagram final : public tracing::IStructureDiagram {
public:
    explicit HardwareDiagram(const tracing::DiagramInput& input)
        : boxes_(input.primitiveBounds.begin(), input.primitiveBounds.end())
    {}

    tracing::StructureCut cut(float y, int /*detail*/) const override
    {
        tracing::StructureCut cut;
        for (const core::Aabb& box : boxes_) {
            if (box.lo.y <= y && y <= box.hi.y)
                cut.rects.push_back({{box.lo.x, box.lo.z}, {box.hi.x, box.hi.z}, tracing::CutRect::Kind::Full});
        }
        cut.text = std::format("The {} brick boxes that cross this cut. The program gives the GPU only these boxes. "
                               "The GPU builds its own tree from them and does not show that tree.",
                               cut.rects.size());
        cut.legend = {{tracing::CutRect::Kind::Full, "Brick box given to the GPU"}};
        return cut;
    }

    tracing::WalkText walkText() const override
    {
        return {"", "candidate brick from RT traversal",
                "Hardware: the driver builds its own BVH from the brick AABBs, and the RT cores walk it. The shader "
                "only sees the candidate bricks that the traversal reports, and runs the same brick test as the "
                "software tracers."};
    }

    // The GPU logs no steps of its own BVH: the log holds only the primitives it hands over.
    std::optional<tracing::WalkStep> walkStep(std::span<const core::RayLogEntry> /*steps*/, std::size_t /*i*/,
                                              glm::vec3 /*origin*/, glm::vec3 /*direction*/, float /*closest*/) override
    {
        return std::nullopt;
    }

private:
    std::vector<core::Aabb> boxes_;
};

} // namespace

std::unique_ptr<tracing::IStructureDiagram> makeHardwareDiagram(const tracing::DiagramInput& input)
{
    return std::make_unique<HardwareDiagram>(input);
}

} // namespace thesis::tracers
