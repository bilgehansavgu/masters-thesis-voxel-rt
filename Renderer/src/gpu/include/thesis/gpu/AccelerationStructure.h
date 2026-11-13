#pragma once

#include "thesis/gpu/Buffer.h"

#include <vulkan/vulkan.h>

#include <cstdint>

namespace thesis::gpu {

class Context;

// The GPU's own search structure (bottom or top level), with the buffer that stores it.
class AccelerationStructure {
public:
    AccelerationStructure() = default;
    AccelerationStructure(const Context& context, VkAccelerationStructureTypeKHR type, VkDeviceSize size);
    ~AccelerationStructure();
    AccelerationStructure(AccelerationStructure&& other) noexcept;
    AccelerationStructure& operator=(AccelerationStructure&& other) noexcept;
    AccelerationStructure(const AccelerationStructure&) = delete;
    AccelerationStructure& operator=(const AccelerationStructure&) = delete;

    [[nodiscard]] VkAccelerationStructureKHR handle() const { return handle_; }
    [[nodiscard]] VkDeviceAddress address() const { return address_; }
    [[nodiscard]] VkDeviceSize size() const { return storage_.size(); }

private:
    void release() noexcept;

    const Context* context_ = nullptr;
    Buffer storage_;
    VkAccelerationStructureKHR handle_ = VK_NULL_HANDLE;
    VkDeviceAddress address_ = 0;
};

struct AccelerationStructureBuild {
    AccelerationStructure structure;
    double gpuMilliseconds = 0.0;
    VkDeviceSize scratchBytes = 0; // temporary memory the build needed (freed afterwards)
};

// Builds one acceleration structure from a single geometry and waits for it. The build's GPU
// time is measured with timestamp queries.
[[nodiscard]] AccelerationStructureBuild buildAccelerationStructure(const Context& context,
                                                                    VkAccelerationStructureTypeKHR type,
                                                                    const VkAccelerationStructureGeometryKHR& geometry,
                                                                    std::uint32_t primitiveCount,
                                                                    VkBuildAccelerationStructureFlagsKHR flags);

} // namespace thesis::gpu
