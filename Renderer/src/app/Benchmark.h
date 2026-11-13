#pragma once

#include "Options.h"

namespace thesis::app {

// Headless benchmark: for each tracer, warm up, then run `repeats` orbits of `frames` frames,
// one frame at a time, and write a results file per tracer. Returns the process exit code.
int runBenchmark(const Options& options);

} // namespace thesis::app
