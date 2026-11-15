#pragma once

// ============================================================================
// READING ORDER: FILE 20B (Optional GPU plumbing: loading & launching shaders)
// ============================================================================

#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace thesis::gpu {

class Buffer;
class Context;

struct KernelBinding {
    std::uint32_t binding;
    VkDescriptorType type;
};

struct KernelDescription {
    std::filesystem::path spirvPath;     // path to the compiled `.spv` shader file
    std::vector<KernelBinding> bindings; // all in descriptor set 0
    std::uint32_t pushConstantSize = 0;  // size in bytes of the C++ push-constant struct
};

// The resources for one dispatch. Bound with push descriptors (`VK_KHR_push_descriptor`):
// recorded straight into the command buffer, so there are no descriptor pools and nothing that
// can go stale.
//
// C++ REFRESHER: Method chaining
// Notice that `buffer(...)`, `storageImage(...)`, and `accelerationStructure(...)` return
// `KernelResources&` (`return *this;`). That lets callers chain calls on one line:
//   resources.buffer(0, batch.rays).buffer(1, batch.hits);
class KernelResources {
public:
    KernelResources& buffer(std::uint32_t binding, const Buffer& buffer);
    KernelResources& storageImage(std::uint32_t binding, VkImageView view);
    KernelResources& accelerationStructure(std::uint32_t binding, VkAccelerationStructureKHR structure);

private:
    friend class ComputeKernel;
    struct Entry {
        std::uint32_t binding;
        VkDescriptorType type;
        VkBuffer buffer = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkAccelerationStructureKHR structure = VK_NULL_HANDLE;
    };
    std::vector<Entry> entries_;
};

// One compute shader (SPIR-V file, entry point "main") with its pipeline and layout.
//
// On creation it reads the shader's reflection report (<name>.json next to the .spv, written by
// the build) and throws if the shader and the C++ side disagree: a resource the shader reads at
// a binding the description does not provide, a different descriptor type, or a push-constant
// block whose size differs from the C++ struct. Bindings the shader does not read are allowed.
class ComputeKernel {
public:
    ComputeKernel(const Context& context, const KernelDescription& description);
    ~ComputeKernel();
    ComputeKernel(const ComputeKernel&) = delete;
    ComputeKernel& operator=(const ComputeKernel&) = delete;

    void dispatch(VkCommandBuffer cmd, const KernelResources& resources, std::uint32_t groupsX, std::uint32_t groupsY,
                  std::uint32_t groupsZ, const void* pushConstants = nullptr, std::uint32_t pushConstantSize = 0) const;

    // Convenience template overload: lets you pass any C++ struct `params` directly by reference
    // instead of manually passing `&params` and `sizeof(params)`.
    template <typename PushConstants>
    void dispatch(VkCommandBuffer cmd, const KernelResources& resources, std::uint32_t groupsX, std::uint32_t groupsY,
                  std::uint32_t groupsZ, const PushConstants& pushConstants) const
    {
        dispatch(cmd, resources, groupsX, groupsY, groupsZ, &pushConstants,
                 static_cast<std::uint32_t>(sizeof(PushConstants)));
    }

    [[nodiscard]] const std::string& name() const { return name_; }

private:
    const Context& context_;
    std::string name_;
    std::uint32_t pushConstantSize_ = 0;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

// Number of workgroups needed to cover `count` items with groups of `groupSize` (integer ceiling
// division: e.g., 1920 pixels with groupSize 8 -> (1920 + 7) / 8 = 240 groups).
[[nodiscard]] constexpr std::uint32_t groupCount(std::uint32_t count, std::uint32_t groupSize)
{
    return (count + groupSize - 1) / groupSize;
}

} // namespace thesis::gpu
