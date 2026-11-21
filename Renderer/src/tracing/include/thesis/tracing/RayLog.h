#pragma once

// Side: CPU

#include "thesis/core/GpuTypes.h"
#include "thesis/shared/RayLog.h"

#include <cstdint>
#include <vector>

namespace thesis::tracing {

inline constexpr std::uint32_t kNoLoggedRay = THESIS_LOG_NO_RAY;

// The steps of one ray, in the order in which the counting variant of a tracer took them. The
// meaning of each entry: thesis/shared/RayLog.h.
struct RayLog {
    std::uint32_t ray = kNoLoggedRay;
    std::vector<core::RayLogEntry> steps;
    std::uint32_t lostSteps = 0; // steps that did not fit into the buffer (more than THESIS_RAY_LOG_CAPACITY)
};

// The work counters of the logged ray, counted from its steps. Equal to the core::RayWork that the
// counting variant wrote for that ray (hitCell is not in the log and stays 0).
[[nodiscard]] core::RayWork countWork(const RayLog& log);

} // namespace thesis::tracing
