#pragma once

// Side: GPU

// Order matters: TracyVulkan.hpp needs the Vulkan header first (separate blocks keep the
// formatter from sorting them).
#include <vulkan/vulkan.h>

#include <VkBootstrap.h>
#include <vk_mem_alloc.h>

#include <tracy/TracyVulkan.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace thesis::gpu {

struct ContextOptions {
    std::string applicationName = "Thesis Renderer";
    // Turns on the Khronos validation layer. Every warning or error it reports is printed and
    // counted (see validationMessageCount()), so tests can fail on them.
    bool validation = false;
    // Extra instance extensions, e.g. what GLFW needs for a window surface.
    std::vector<std::string> instanceExtensions;
    // Creates the window surface once the instance exists. Leave empty for headless use.
    std::function<VkSurfaceKHR(VkInstance)> createSurface;
};

// Facts about the selected GPU that measurements and results files need.
struct DeviceInfo {
    std::string name;
    std::string driverName;
    std::string driverInfo; // e.g. the NVIDIA driver version "617.42"
    std::uint32_t vendorId = 0;
    std::uint32_t deviceId = 0;
    std::uint32_t apiVersion = 0;
    double timestampPeriodNs = 1.0;
    std::uint32_t timestampValidBits = 64;
    VkDeviceSize minScratchOffsetAlignment = 256;
    VkDeviceSize deviceLocalBytes = 0;
};

// Number of validation warnings and errors reported since the program started.
[[nodiscard]] std::uint64_t validationMessageCount();

// Owns the Vulkan instance, the device (always a GPU with hardware ray tracing support), the one
// queue used for everything, the memory allocator, and the Tracy GPU profiling context.
class Context {
public:
    explicit Context(const ContextOptions& options);
    ~Context();
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    [[nodiscard]] VkInstance instance() const { return instance_.instance; }
    [[nodiscard]] VkPhysicalDevice physicalDevice() const { return physicalDevice_.physical_device; }
    [[nodiscard]] VkDevice device() const { return device_.device; }
    [[nodiscard]] VkQueue queue() const { return queue_; }
    [[nodiscard]] std::uint32_t queueFamily() const { return queueFamily_; }
    [[nodiscard]] VkSurfaceKHR surface() const { return surface_; }
    [[nodiscard]] VmaAllocator allocator() const { return allocator_; }
    [[nodiscard]] const vkb::Device& bootstrapDevice() const { return device_; }
    // Function pointers for extension commands (acceleration structures, push descriptors).
    [[nodiscard]] const vkb::DispatchTable& dispatch() const { return dispatch_; }
    [[nodiscard]] const DeviceInfo& info() const { return info_; }
    [[nodiscard]] TracyVkCtx tracy() const { return tracy_; }

    // Records commands with `record`, submits them and blocks until the GPU has finished.
    void submitAndWait(const std::function<void(VkCommandBuffer)>& record) const;

    // True if the driver reports what it made of each shader: registers, instructions
    // (VK_KHR_pipeline_executable_properties). ComputeKernel::statistics() has the numbers.
    [[nodiscard]] bool pipelineStatistics() const { return pipelineStatistics_; }

    // Bytes of device-local memory currently in use by this process (VK_EXT_memory_budget).
    [[nodiscard]] VkDeviceSize deviceLocalUsage() const;

    // Debug labels (VK_EXT_debug_utils): a named range of commands, shown by Nsight Graphics and
    // RenderDoc. Both do nothing where the loader does not offer the extension. Use DebugLabel.
    void beginLabel(VkCommandBuffer cmd, const char* name) const;
    void endLabel(VkCommandBuffer cmd) const;

private:
    void queryDeviceInfo();

    vkb::Instance instance_{};
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    vkb::PhysicalDevice physicalDevice_{};
    vkb::Device device_{};
    vkb::DispatchTable dispatch_{};
    VkQueue queue_ = VK_NULL_HANDLE;
    std::uint32_t queueFamily_ = 0;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    DeviceInfo info_{};
    bool memoryBudget_ = false;
    bool pipelineStatistics_ = false;
    PFN_vkCmdBeginDebugUtilsLabelEXT beginLabel_ = nullptr;
    PFN_vkCmdEndDebugUtilsLabelEXT endLabel_ = nullptr;

    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
    TracyVkCtx tracy_ = nullptr;
};

// A debug label around the commands recorded while it lives: one named pass in Nsight Graphics
// and RenderDoc.
class DebugLabel {
public:
    DebugLabel(const Context& context, VkCommandBuffer cmd, const char* name) : context_(context), cmd_(cmd)
    {
        context_.beginLabel(cmd_, name);
    }
    ~DebugLabel() { context_.endLabel(cmd_); }
    DebugLabel(const DebugLabel&) = delete;
    DebugLabel& operator=(const DebugLabel&) = delete;

private:
    const Context& context_;
    VkCommandBuffer cmd_;
};

} // namespace thesis::gpu
