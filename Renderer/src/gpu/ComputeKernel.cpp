#include "thesis/gpu/ComputeKernel.h"

#include "ShaderInterface.h"
#include "thesis/gpu/Buffer.h"
#include "thesis/gpu/Check.h"
#include "thesis/gpu/Context.h"

#include <stdexcept>
#include <string>

namespace thesis::gpu {

KernelResources& KernelResources::buffer(std::uint32_t binding, const Buffer& buffer)
{
    Entry entry{binding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER};
    entry.buffer = buffer.handle();
    entries_.push_back(entry);
    return *this;
}

KernelResources& KernelResources::storageImage(std::uint32_t binding, VkImageView view)
{
    Entry entry{binding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE};
    entry.view = view;
    entries_.push_back(entry);
    return *this;
}

KernelResources& KernelResources::accelerationStructure(std::uint32_t binding, VkAccelerationStructureKHR structure)
{
    Entry entry{binding, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR};
    entry.structure = structure;
    entries_.push_back(entry);
    return *this;
}

void KernelResources::push(const Context& context, VkCommandBuffer cmd, VkPipelineBindPoint bindPoint,
                           VkPipelineLayout layout) const
{
    const std::size_t count = entries_.size();
    std::vector<VkWriteDescriptorSet> writes(count);
    std::vector<VkDescriptorBufferInfo> bufferInfos(count);
    std::vector<VkDescriptorImageInfo> imageInfos(count);
    std::vector<VkWriteDescriptorSetAccelerationStructureKHR> structureInfos(count);

    for (std::size_t i = 0; i < count; ++i) {
        const Entry& entry = entries_[i];
        VkWriteDescriptorSet& write = writes[i];
        write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstBinding = entry.binding;
        write.descriptorCount = 1;
        write.descriptorType = entry.type;
        switch (entry.type) {
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            bufferInfos[i] = {entry.buffer, 0, VK_WHOLE_SIZE};
            write.pBufferInfo = &bufferInfos[i];
            break;
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            imageInfos[i] = {VK_NULL_HANDLE, entry.view, VK_IMAGE_LAYOUT_GENERAL};
            write.pImageInfo = &imageInfos[i];
            break;
        case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
            structureInfos[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
            structureInfos[i].accelerationStructureCount = 1;
            structureInfos[i].pAccelerationStructures = &entry.structure;
            write.pNext = &structureInfos[i];
            break;
        default:
            throw std::logic_error("KernelResources: unsupported descriptor type");
        }
    }
    context.dispatch().cmdPushDescriptorSetKHR(cmd, bindPoint, layout, 0, static_cast<std::uint32_t>(writes.size()),
                                               writes.data());
}

ComputeKernel::ComputeKernel(const Context& context, const KernelDescription& description)
    : context_(context),
      name_(description.spirvPath.filename().string()),
      pushConstantSize_(description.pushConstantSize)
{
    checkAgainstReflection(description.spirvPath, description.bindings, description.pushConstantSize, name_);
    const VkDevice device = context.device();

    setLayout_ = createPushDescriptorSetLayout(device, description.bindings, VK_SHADER_STAGE_COMPUTE_BIT);

    VkPushConstantRange pushRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, pushConstantSize_};
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

    VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = module;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = pipelineLayout_;
    if (context_.pipelineStatistics()) {
        pipelineInfo.flags |= VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR;
    }
    const VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_);
    vkDestroyShaderModule(device, module, nullptr);
    THESIS_VK_CHECK(result);
    statistics_ = readPipelineStatistics(context_, pipeline_);
}

ComputeKernel::~ComputeKernel()
{
    const VkDevice device = context_.device();
    vkDestroyPipeline(device, pipeline_, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout_, nullptr);
    vkDestroyDescriptorSetLayout(device, setLayout_, nullptr);
}

void ComputeKernel::dispatch(VkCommandBuffer cmd, const KernelResources& resources, std::uint32_t groupsX,
                             std::uint32_t groupsY, std::uint32_t groupsZ, const void* pushConstants,
                             std::uint32_t pushConstantSize) const
{
    if (pushConstantSize != pushConstantSize_) {
        throw std::logic_error("ComputeKernel '" + name_ + "': push constant size mismatch");
    }
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    resources.push(context_, cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_);
    if (pushConstantSize_ > 0) {
        vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, pushConstantSize_, pushConstants);
    }
    vkCmdDispatch(cmd, groupsX, groupsY, groupsZ);
}

} // namespace thesis::gpu
