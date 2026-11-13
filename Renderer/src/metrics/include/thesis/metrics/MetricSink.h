#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace thesis::metrics {

struct StageSample {
    const char* stage; // static string (see thesis/gpu/GpuTimer.h)
    double milliseconds;
};

struct FrameSample {
    std::uint32_t repeat = 0;
    std::uint64_t frame = 0;
    std::uint64_t rayCount = 0;
    std::vector<StageSample> stages;
};

// THE MEASUREMENT OUTPUT SOCKET: where numbers go. The code that measures does not know
// whether its numbers end up on the Tracy timeline, in a results file, or both.
class IMetricSink {
public:
    virtual ~IMetricSink() = default;
    // `run` describes the run: tracer, device, configuration, system state, ...
    virtual void beginRun(const nlohmann::json& run) = 0;
    virtual void recordFrame(const FrameSample& frame) = 0;
    virtual void endRun(const nlohmann::json& summary) = 0;
};

} // namespace thesis::metrics
