#pragma once

// Side: CPU

#include "thesis/tracing/StructureDiagram.h"

#include <vector>

namespace thesis::tracing {

// The diagram of a tracer whose search structure the GPU builds and walks without showing it (a
// BVH built by the driver). Only the input of that build, the primitive boxes, can be drawn, and
// the ray log holds no steps of the structure. The tracer gives the words of its walk.
class HiddenStructureDiagram final : public IStructureDiagram {
public:
    HiddenStructureDiagram(const DiagramInput& input, WalkText walkText);

    [[nodiscard]] StructureCut cut(float y, int detail) const override;
    [[nodiscard]] WalkText walkText() const override { return walkText_; }
    // The GPU logs no steps of its own structure: the log holds only the primitives it hands over.
    [[nodiscard]] std::optional<WalkStep> walkStep(std::span<const core::RayLogEntry> steps, std::size_t i,
                                                   glm::vec3 origin, glm::vec3 direction, float closest) override;

private:
    std::vector<core::Aabb> boxes_;
    WalkText walkText_;
};

} // namespace thesis::tracing
