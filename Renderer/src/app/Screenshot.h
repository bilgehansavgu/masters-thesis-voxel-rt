#pragma once

// Side: GPU

#include "Options.h"

namespace thesis::app {

// Renders one frame without a window and writes it to options.screenshot as a PNG file.
// It uses the same frame pipeline as the window and the benchmark. A count view gets its colour
// bar in a strip below the image. Returns the process exit code.
int runScreenshot(const Options& options);

} // namespace thesis::app
