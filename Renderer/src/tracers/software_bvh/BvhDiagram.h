#pragma once

// Side: CPU

#include "thesis/tracing/StructureDiagram.h"

#include <memory>

namespace thesis::tracers {

// The diagram of the BVH tracer (ITracer::diagram).
[[nodiscard]] std::unique_ptr<tracing::IStructureDiagram> makeBvhDiagram(const tracing::DiagramInput& input);

} // namespace thesis::tracers
