#pragma once

// Side: GPU

#include "Options.h"

#include "ViewComparison.h"

#include <string>

namespace thesis::app {

// Runs both encodings or two tracers at one pose, using the headless benchmark protocol.
ViewComparison compareView(Options options, const std::string& secondTracer = {});
void restoreResult(Options& options);

// Headless benchmark: for each tracer, warm up, then run `repeats` orbits of `frames` frames,
// one frame at a time, and write a results file per tracer. Returns the process exit code.
int runBenchmark(const Options& options);

} // namespace thesis::app
