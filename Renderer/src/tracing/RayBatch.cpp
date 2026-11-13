#include "thesis/tracing/RayBatch.h"

#include "thesis/core/GpuTypes.h"

#include <stdexcept>

namespace thesis::tracing {

RayBatch RayBatch::create(const gpu::Context& context, std::uint32_t width, std::uint32_t height)
{
    if (width == 0 || height == 0) {
        throw std::invalid_argument("RayBatch: width and height must be positive");
    }
    RayBatch batch;
    batch.width = width;
    batch.height = height;
    const VkDeviceSize count = static_cast<VkDeviceSize>(width) * height;
    batch.rays = gpu::Buffer(context, count * sizeof(core::Ray), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                             gpu::MemoryUsage::DeviceLocal);
    batch.hits = gpu::Buffer(context, count * sizeof(core::Hit), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                             gpu::MemoryUsage::DeviceLocal);
    return batch;
}

} // namespace thesis::tracing
