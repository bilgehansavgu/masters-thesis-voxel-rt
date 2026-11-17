#pragma once

#include "thesis/core/GpuTypes.h"
#include "thesis/gpu/Buffer.h"
#include "thesis/shared/RayLog.h"

#include <vulkan/vulkan.h>

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

// The GPU buffer of the ray log (binding THESIS_BINDING_RAY_LOG) and its copy for the CPU.
//
//   request()   before the trace: names the ray to log (or kNoLoggedRay) and clears the header
//   the counting variant of a tracer writes the steps of that ray (WorkOutput::log = buffer())
//   copyBack()  after the trace: copies the log to memory that the CPU can read
//   read()      once those commands have finished on the GPU
class RayLogBuffer {
public:
    explicit RayLogBuffer(const gpu::Context& context);

    void request(VkCommandBuffer cmd, std::uint32_t ray) const;
    void copyBack(VkCommandBuffer cmd) const;
    [[nodiscard]] RayLog read() const;

    [[nodiscard]] const gpu::Buffer& buffer() const { return gpuBuffer_; }

private:
    gpu::Buffer gpuBuffer_;
    gpu::Buffer cpuCopy_;
};

} // namespace thesis::tracing
