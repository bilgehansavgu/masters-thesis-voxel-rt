#pragma once

// Side: CPU

namespace thesis::gpu {

// The GPU time of one named stage of a frame, measured by GpuTimer.
struct StageTiming {
    const char* stage; // static string, also used as the Tracy zone / plot name
    double milliseconds;
};

} // namespace thesis::gpu
