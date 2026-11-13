#pragma once

#include "Options.h"

namespace thesis::app {

// The window: orbit the scene, switch tracers with Tab (or the panel), switch views with 1-4.
// Returns the process exit code.
int runInteractive(const Options& options);

} // namespace thesis::app
