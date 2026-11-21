#pragma once

// Side: GPU

#include "thesis/gpu/Buffer.h"
#include "thesis/tracing/RayLog.h"

#include <vulkan/vulkan.h>

#include <cstdint>

namespace thesis::tracing {

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
