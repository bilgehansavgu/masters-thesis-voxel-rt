#pragma once

// Side: GPU

// Order matters: TracyVulkan.hpp needs the Vulkan header first (separate blocks keep the
// formatter from sorting them).
#include <vulkan/vulkan.h>

#include <tracy/TracyVulkan.hpp>

#include "thesis/gpu/StageTiming.h"

#include <cstdint>
#include <vector>

namespace thesis::gpu {

class Context;

// Measures GPU time of named stages with timestamp queries. One frame at a time:
//   reset(cmd) → scopes → submit → wait for the GPU → resolve()
// These numbers are the source of truth for results files. Tracy shows the same stages on its
// timeline (see THESIS_GPU_SCOPE) but is not used for the numbers.
class GpuTimer {
public:
    GpuTimer(const Context& context, std::uint32_t maxScopes = 32);
    ~GpuTimer();
    GpuTimer(const GpuTimer&) = delete;
    GpuTimer& operator=(const GpuTimer&) = delete;

    void reset(VkCommandBuffer cmd);
    [[nodiscard]] std::uint32_t begin(VkCommandBuffer cmd, const char* stage);
    void end(VkCommandBuffer cmd, std::uint32_t scope);

    // Call only after the command buffer has finished executing. Stages in the order begun.
    [[nodiscard]] std::vector<StageTiming> resolve();

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueryPool pool_ = VK_NULL_HANDLE;
    std::uint32_t capacity_ = 0;
    double periodNs_ = 1.0;
    std::uint64_t validMask_ = ~0ull;
    std::vector<const char*> stages_;
};

// RAII pair of begin()/end().
class GpuScope {
public:
    GpuScope(GpuTimer& timer, VkCommandBuffer cmd, const char* stage)
        : timer_(timer), cmd_(cmd), scope_(timer.begin(cmd, stage))
    {}
    ~GpuScope() { timer_.end(cmd_, scope_); }
    GpuScope(const GpuScope&) = delete;
    GpuScope& operator=(const GpuScope&) = delete;

private:
    GpuTimer& timer_;
    VkCommandBuffer cmd_;
    std::uint32_t scope_;
};

} // namespace thesis::gpu

#define THESIS_GPU_CONCAT_INNER(a, b) a##b
#define THESIS_GPU_CONCAT(a, b) THESIS_GPU_CONCAT_INNER(a, b)

// Times the rest of the enclosing C++ scope on the GPU: our timestamp queries (for results)
// and a Tracy GPU zone (for the timeline). `stage` must be a string literal or constexpr string.
#define THESIS_GPU_SCOPE(timer, tracyContext, cmd, stage)                                                              \
    TracyVkNamedZone(tracyContext, THESIS_GPU_CONCAT(thesisTracyGpuZone_, __LINE__), cmd, stage, true);                \
    ::thesis::gpu::GpuScope THESIS_GPU_CONCAT(thesisGpuScope_, __LINE__)(timer, cmd, stage)
