#pragma once

// Side: CPU

#include "thesis/core/GpuTypes.h"
#include "thesis/core/Scene.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace thesis::tracing {

// A rectangle of a horizontal cut through a search structure, in x and z.
struct CutRect {
    enum class Kind {
        Empty, // empty space the structure holds as one piece (an octree cube that is not divided)
        Full,  // a piece that holds a brick, or the box of one brick
        Group, // a box around a group of bricks
        Line,  // legend only: the lines of StructureCut::gridLines
    };
    glm::vec2 lo{0.0f};
    glm::vec2 hi{0.0f};
    Kind kind = Kind::Full;
};

// Lines of a regular grid in the cut: count.x + 1 lines along z and count.y + 1 lines along x.
struct CutGridLines {
    glm::vec2 origin{0.0f};   // x and z
    glm::vec2 cellSize{0.0f}; // x and z
    glm::uvec2 count{0};      // cells along x and z
};

// A horizontal cut through the search structure at one height, as plain shapes. The window draws
// them and needs no knowledge of the structure.
struct StructureCut {
    std::vector<CutRect> rects;
    std::optional<CutGridLines> gridLines;
    std::string text; // what the cut shows, with its counts, in one paragraph
    struct LegendEntry {
        CutRect::Kind kind;
        std::string text;
    };
    std::vector<LegendEntry> legend;
};

// A control that changes how much of the structure a cut shows (for example a tree level).
struct DiagramDetail {
    std::string name; // label of the slider
    int min = 0;
    int max = 0;
    int initial = 0;
};

// One step of the ray through the search structure, from the ray log of the counting variant.
struct WalkStep {
    std::string label;
    std::vector<core::Aabb> boxes; // what the step looked at, drawn on the model
    std::optional<float> t;        // where the step enters along the ray
    bool positive = false;         // the step leads to a brick (or enters a box)
};

// How a ray walk through this structure is described in the window.
struct WalkText {
    // One step through the search structure, plural (for example "node reads"). Empty if the tracer
    // cannot see its own steps (the hardware tracer: the GPU walks its BVH without telling).
    std::string stepName;
    std::string brickTest; // what a test of one primitive is called, for example "brick test"
    std::string tooltip;   // how the walk works, a few sentences
};

// What a tracer shows of its search structure. It is built on the CPU with the same builder as the
// structure on the GPU, so what is drawn is what the tracer walks. Made by ITracer::diagram().
class IStructureDiagram {
public:
    virtual ~IStructureDiagram() = default;

    // The extra control of the cut, if the structure has one.
    [[nodiscard]] virtual std::optional<DiagramDetail> detail() const { return std::nullopt; }
    // The cut at height y; `detail` is the value of detail(), or 0.
    [[nodiscard]] virtual StructureCut cut(float y, int detail) const = 0;

    [[nodiscard]] virtual WalkText walkText() const = 0;
    // Called before the steps of a new ray.
    virtual void beginWalk() {}
    // steps[i] as a step through the structure, or none if it is not one of the structure's own
    // events (primitive tests and hits are described by the caller). The steps after i may be
    // read to tell what the step led to. `closest` is the closest hit found before steps[i].
    [[nodiscard]] virtual std::optional<WalkStep> walkStep(std::span<const core::RayLogEntry> steps, std::size_t i,
                                                           glm::vec3 origin, glm::vec3 direction, float closest) = 0;
};

// What a tracer needs to build its diagram: the scene as the tracer sees it.
struct DiagramInput {
    std::span<const core::Aabb> primitiveBounds;
    core::Bounds bounds;
    std::optional<core::RegularGrid> primitiveGrid;
};

// The kind of ray log event that follows steps[i], skipping events inside a brick test, and its
// index (steps.size() if none follows). For walkStep() implementations.
[[nodiscard]] std::pair<std::uint32_t, std::size_t> nextStructureEvent(std::span<const core::RayLogEntry> steps,
                                                                       std::size_t i);

} // namespace thesis::tracing
