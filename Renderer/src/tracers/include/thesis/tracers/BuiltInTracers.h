#pragma once

// Side: GPU

#include "thesis/tracing/Tracer.h"

namespace thesis::plugins {

// Adds every tracer plugin that was built into this program (generated at configure time).
void registerBuiltInTracers(tracing::TracerRegistry& registry);

} // namespace thesis::plugins
