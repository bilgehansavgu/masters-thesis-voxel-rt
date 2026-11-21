#include "thesis/gpu/RayTracingPipeline.h"

#include "ShaderInterface.h"
#include "thesis/gpu/Check.h"
#include "thesis/gpu/Context.h"

#include <cstring>
#include <stdexcept>

namespace thesis::gpu {
namespace {

constexpr VkShaderStageFlags kAllStages = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR |
                                          VK_SHADER_STAGE_INTERSECTION_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

constexpr VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment)
{
    return (value + alignment - 1) / alignment * alignment;
}

VkRayTracingShaderGroupCreateInfoKHR generalGroup(std::uint32_t stage)
{
    VkRayTracingShaderGroupCreateInfoKHR group{VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
    group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    group.generalShader = stage;
    group.closestHitShader = VK_SHADER_UNUSED_KHR;
    group.anyHitShader = VK_SHADER_UNUSED_KHR;
    group.intersectionShader = VK_SHADER_UNUSED_KHR;
    return group;
}

} // namespace

RayTracingPipeline::RayTracingPipeline(const Context& context, const RayTracingPipelineDescription& description)
    : context_(context),
      name_(description.spirvPath.filename().string() + " (" + description.rayGeneration + ")"),
      pushConstantSize_(description.pushConstantSize)
{
    if (!context.rayTracingPipelines()) {
        throw std::runtime_error("RayTracingPipeline '" + name_ +
                                 "': the device has no ray tracing pipelines (VK_KHR_ray_tracing_pipeline)");
    }
    if (description.rayGeneration.empty() || description.miss.empty() || description.intersection.empty()) {
        throw std::logic_error("RayTracingPipeline '" + name_ +
                               "': ray generation, miss and intersection entry points are required");
    }
    checkAgainstReflection(description.spirvPath, description.bindings, description.pushConstantSize, name_);
    const VkDevice device = context.device();

    setLayout_ = createPushDescriptorSetLayout(device, description.bindings, kAllStages);
    VkPushConstantRange pushRange{kAllStages, 0, pushConstantSize_};
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &setLayout_;
    pipelineLayoutInfo.pushConstantRangeCount = pushConstantSize_ > 0 ? 1 : 0;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;
    THESIS_VK_CHECK(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout_));

    const std::vector<std::uint32_t> code = readSpirv(description.spirvPath);
    VkShaderModuleCreateInfo moduleInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    moduleInfo.codeSize = code.size() * sizeof(std::uint32_t);
    moduleInfo.pCode = code.data();
    VkShaderModule module = VK_NULL_HANDLE;
    THESIS_VK_CHECK(vkCreateShaderModule(device, &moduleInfo, nullptr, &module));

    // Stages: 0 ray generation, 1 miss, 2 intersection, 3 closest hit (if any).
    std::vector<VkPipelineShaderStageCreateInfo> stages;
    const auto addStage = [&](VkShaderStageFlagBits stage, const std::string& entry) {
        VkPipelineShaderStageCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        info.stage = stage;
        info.module = module;
        info.pName = entry.c_str();
        stages.push_back(info);
        return static_cast<std::uint32_t>(stages.size() - 1);
    };
    const std::uint32_t rayGenerationStage = addStage(VK_SHADER_STAGE_RAYGEN_BIT_KHR, description.rayGeneration);
    const std::uint32_t missStage = addStage(VK_SHADER_STAGE_MISS_BIT_KHR, description.miss);
    const std::uint32_t intersectionStage = addStage(VK_SHADER_STAGE_INTERSECTION_BIT_KHR, description.intersection);
    const std::uint32_t closestHitStage = description.closestHit.empty()
                                              ? VK_SHADER_UNUSED_KHR
                                              : addStage(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, description.closestHit);

    // Groups: 0 ray generation, 1 miss, 2 the hit group of the boxes.
    std::vector<VkRayTracingShaderGroupCreateInfoKHR> groups{generalGroup(rayGenerationStage), generalGroup(missStage)};
    VkRayTracingShaderGroupCreateInfoKHR hitGroup{VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
    hitGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR;
    hitGroup.generalShader = VK_SHADER_UNUSED_KHR;
    hitGroup.closestHitShader = closestHitStage;
    hitGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
    hitGroup.intersectionShader = intersectionStage;
    groups.push_back(hitGroup);

    VkRayTracingPipelineCreateInfoKHR pipelineInfo{VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR};
    pipelineInfo.stageCount = static_cast<std::uint32_t>(stages.size());
    pipelineInfo.pStages = stages.data();
    pipelineInfo.groupCount = static_cast<std::uint32_t>(groups.size());
    pipelineInfo.pGroups = groups.data();
    pipelineInfo.maxPipelineRayRecursionDepth = 1;
    pipelineInfo.layout = pipelineLayout_;
    if (context_.pipelineStatistics()) {
        pipelineInfo.flags |= VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR;
    }
    const VkResult result = context_.dispatch().createRayTracingPipelinesKHR(VK_NULL_HANDLE, VK_NULL_HANDLE, 1,
                                                                             &pipelineInfo, nullptr, &pipeline_);
    vkDestroyShaderModule(device, module, nullptr);
    THESIS_VK_CHECK(result);
    statistics_ = readPipelineStatistics(context_, pipeline_);

    // The shader binding table: one record per group, each region starting at the base
    // alignment. The records hold only the group handles; the shaders read everything else from
    // descriptors and push constants.
    const DeviceInfo& info = context_.info();
    const VkDeviceSize handleSize = info.shaderGroupHandleSize;
    const VkDeviceSize recordStride = alignUp(handleSize, info.shaderGroupHandleAlignment);
    const VkDeviceSize regionSize = alignUp(recordStride, info.shaderGroupBaseAlignment);
    const std::uint32_t groupCount = static_cast<std::uint32_t>(groups.size());
    std::vector<std::byte> handles(groupCount * handleSize);
    THESIS_VK_CHECK(context_.dispatch().getRayTracingShaderGroupHandlesKHR(pipeline_, 0, groupCount, handles.size(),
                                                                           handles.data()));
    std::vector<std::byte> table(groupCount * regionSize);
    for (std::uint32_t group = 0; group < groupCount; ++group) {
        std::memcpy(table.data() + group * regionSize, handles.data() + group * handleSize, handleSize);
    }
    bindingTable_ = uploadBuffer(
        context_, table, VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        info.shaderGroupBaseAlignment);
    const VkDeviceAddress base = bindingTable_.address();
    // The ray generation region's stride must equal its size.
    rayGenerationRegion_ = {base, regionSize, regionSize};
    missRegion_ = {base + regionSize, recordStride, regionSize};
    hitRegion_ = {base + 2 * regionSize, recordStride, regionSize};
}

RayTracingPipeline::~RayTracingPipeline()
{
    const VkDevice device = context_.device();
    vkDestroyPipeline(device, pipeline_, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout_, nullptr);
    vkDestroyDescriptorSetLayout(device, setLayout_, nullptr);
}

void RayTracingPipeline::trace(VkCommandBuffer cmd, const KernelResources& resources, std::uint32_t width,
                               std::uint32_t height, std::uint32_t depth, const void* pushConstants,
                               std::uint32_t pushConstantSize) const
{
    if (pushConstantSize != pushConstantSize_) {
        throw std::logic_error("RayTracingPipeline '" + name_ + "': push constant size mismatch");
    }
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline_);
    resources.push(context_, cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipelineLayout_);
    if (pushConstantSize_ > 0) {
        vkCmdPushConstants(cmd, pipelineLayout_, kAllStages, 0, pushConstantSize_, pushConstants);
    }
    const VkStridedDeviceAddressRegionKHR noCallables{};
    context_.dispatch().cmdTraceRaysKHR(cmd, &rayGenerationRegion_, &missRegion_, &hitRegion_, &noCallables, width,
                                        height, depth);
}

} // namespace thesis::gpu
