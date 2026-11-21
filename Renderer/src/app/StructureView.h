#pragma once

// Side: View

#include "Overlay.h"
#include "RayLesson.h"

#include "thesis/tracing/StructureDiagram.h"

#include <cstddef>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

namespace thesis::app {

// Makes the diagram of one tracer's search structure (tracing::ITracer::diagram); null if the
// tracer has none.
using DiagramFactory = std::function<std::unique_ptr<tracing::IStructureDiagram>(const tracing::DiagramInput&)>;

// Shows the search structure of one tracer on the model: a horizontal cut through it at a height
// that the user picks. The tracer describes its structure (tracing::IStructureDiagram); this class
// only draws the shapes and the text, so it works for every tracer without knowing it.
class StructureView {
public:
    // The panel controls. `tracer` is the index of the tracer in the panel, `make` its diagram.
    void controls(std::size_t tracer, const DiagramFactory& make, const LessonGeometry& scene,
                  const core::Bounds& bounds);
    [[nodiscard]] bool visible() const { return visible_; }
    void show(bool on) { visible_ = on; }
    void draw(const OverlayPainter& painter) const;

    // The traversal of one recorded ray as a stage of the ray walk: one event for each structure
    // step and each brick test, in the order of the GPU log. None if the tracer has no diagram.
    [[nodiscard]] std::optional<RayStage> searchStep(const tracing::RayLog& log, std::size_t tracer,
                                                     const DiagramFactory& make, const LessonGeometry& scene,
                                                     const core::Bounds& bounds, const PrimaryRay& ray);

private:
    static constexpr std::size_t kNone = std::numeric_limits<std::size_t>::max();

    // The diagram of `tracer`, made on first use.
    [[nodiscard]] tracing::IStructureDiagram* diagram(std::size_t tracer, const DiagramFactory& make,
                                                      const LessonGeometry& scene, const core::Bounds& bounds);

    bool visible_ = false;
    std::size_t tracer_ = kNone;
    int cutPercent_ = 50;
    std::vector<std::unique_ptr<tracing::IStructureDiagram>> diagrams_; // by tracer index
    std::vector<bool> made_;                                            // diagram of that tracer asked for
    std::vector<int> details_;                                          // value of each diagram's detail control

    // What the current cut was made for, to make it again only on change.
    std::size_t cutTracer_ = kNone;
    int cutPercentBuilt_ = -1;
    int detailBuilt_ = -1;
    float cutY_ = 0.0f;
    tracing::StructureCut cut_;
};

} // namespace thesis::app
