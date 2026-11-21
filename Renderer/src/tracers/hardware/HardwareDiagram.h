#pragma once

// Side: CPU

#include "thesis/tracing/StructureDiagram.h"

#include <memory>

namespace thesis::tracers {

// The diagram of the hardware tracer (ITracer::diagram). The GPU builds its BVH itself and does
// not show it, so only the input of that build (the brick boxes) can be drawn.
[[nodiscard]] std::unique_ptr<tracing::IStructureDiagram> makeHardwareDiagram(const tracing::DiagramInput& input);

} // namespace thesis::tracers
