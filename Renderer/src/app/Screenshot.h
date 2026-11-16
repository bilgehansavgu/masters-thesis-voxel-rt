#pragma once

#include "Options.h"

namespace thesis::app {

// Renders one frame without a window and writes it to options.screenshot as a PNG file.
// It uses the same frame pipeline as the window and the benchmark. Returns the process exit code.
int runScreenshot(const Options& options);

} // namespace thesis::app
