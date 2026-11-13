#pragma once

#include "Options.h"

#include "thesis/core/Scene.h"
#include "thesis/tracing/Tracer.h"

#include <memory>
#include <string>
#include <vector>

namespace thesis::app {

[[nodiscard]] core::Scene makeScene(const Options& options);

// All tracers built into the program, in registration order.
[[nodiscard]] tracing::TracerRegistry makeTracerRegistry();

// Names from `requested`, or all registered names when it is empty. Throws on unknown names.
[[nodiscard]] std::vector<std::string> selectTracers(const tracing::TracerRegistry& registry,
                                                     const std::vector<std::string>& requested);

} // namespace thesis::app
