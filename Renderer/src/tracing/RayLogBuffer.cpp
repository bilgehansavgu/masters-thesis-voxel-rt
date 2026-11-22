#include "thesis/tracing/RayLogBuffer.h"

#include "thesis/gpu/Context.h"

#include <algorithm>
#include <cstring>

namespace thesis::tracing {
namespace {

constexpr VkDeviceSize kLogBytes = static_cast<VkDeviceSize>(1 + THESIS_RAY_LOG_CAPACITY) * sizeof(core::RayLogEntry);

void memoryBarrier(VkCommandBuffer cmd, VkPipelineStageFlags2 sourceStage, VkAccessFlags2 sourceAccess,
                   VkPipelineStageFlags2 targetStage, VkAccessFlags2 targetAccess)
{
    VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    barrier.srcStageMask = sourceStage;
    barrier.srcAccessMask = sourceAccess;
    barrier.dstStageMask = targetStage;
    barrier.dstAccessMask = targetAccess;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dependency);
}

} // namespace

RayLogBuffer::RayLogBuffer(const gpu::Context& context)
    : shaderStages_(context.storageShaderStages()),
      gpuBuffer_(context, kLogBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, gpu::MemoryUsage::DeviceLocal),
      cpuCopy_(context, kLogBytes, 0, gpu::MemoryUsage::Readback)
{}

void RayLogBuffer::request(VkCommandBuffer cmd, std::uint32_t ray) const
{
    // The shader of the frame before may still read or write the header.
    memoryBarrier(cmd, shaderStages_, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    const core::RayLogEntry header{ray, 0, 0, 0, 0, 0, 0, 0.0f};
    vkCmdUpdateBuffer(cmd, gpuBuffer_.handle(), 0, sizeof(header), &header);
    memoryBarrier(cmd, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, shaderStages_,
                  VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
}

void RayLogBuffer::copyBack(VkCommandBuffer cmd) const
{
    memoryBarrier(cmd, shaderStages_, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                  VK_ACCESS_2_TRANSFER_READ_BIT);
    const VkBufferCopy region{0, 0, kLogBytes};
    vkCmdCopyBuffer(cmd, gpuBuffer_.handle(), cpuCopy_.handle(), 1, &region);
    memoryBarrier(cmd, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
}

RayLog RayLogBuffer::read() const
{
    cpuCopy_.invalidate();
    const auto* entries = static_cast<const core::RayLogEntry*>(cpuCopy_.mapped());
    RayLog log;
    log.ray = entries[0].kind;
    const std::uint32_t count = std::min<std::uint32_t>(entries[0].a, THESIS_RAY_LOG_CAPACITY);
    log.steps.assign(entries + 1, entries + 1 + count);
    log.lostSteps = entries[0].b;
    return log;
}

} // namespace thesis::tracing
