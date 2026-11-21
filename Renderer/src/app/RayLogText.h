#pragma once

// Side: View

#include "thesis/tracing/RayLog.h"

#include <functional>
#include <string>
#include <vector>

namespace thesis::app {

// One step of a ray log as text, for the ray inspector of the panel.
struct RayLogLine {
    int depth = 0; // 0 search structure and result, 1 primitive, 2 inside a brick, 3 a run of a column
    std::string text;
    float t = 0.0f;         // the distance along the ray at which the step begins (or of the hit)
    bool hasT = false;      // steps of the search structure and primitive tests have no distance
    bool important = false; // a hit, or a mipmap square that the walk skipped
};

// Turns a step through the search structure into words; the tracer's ITracer::describeStructureStep.
using StructureStepText = std::function<std::string(const core::RayLogEntry&)>;

// The steps of `log` in words (thesis/shared/RayLog.h says what each entry means).
[[nodiscard]] std::vector<RayLogLine> describeRayLog(const tracing::RayLog& log,
                                                     const StructureStepText& structureStep);

// All lines as one text, indented by depth: for the clipboard.
[[nodiscard]] std::string rayLogAsText(const std::vector<RayLogLine>& lines);

} // namespace thesis::app
