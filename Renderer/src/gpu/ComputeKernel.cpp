#include "thesis/gpu/ComputeKernel.h"

#include "thesis/gpu/Buffer.h"
#include "thesis/gpu/Check.h"
#include "thesis/gpu/Context.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <string>

namespace thesis::gpu {
namespace {

std::vector<std::uint32_t> readSpirv(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Cannot open shader '" + path.string() + "'. Was it built? (target *_shaders)");
    }
    const auto size = static_cast<std::size_t>(file.tellg());
    if (size == 0 || size % 4 != 0) {
        throw std::runtime_error("Shader '" + path.string() + "' is not valid SPIR-V");
    }
    std::vector<std::uint32_t> words(size / 4);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(words.data()), static_cast<std::streamsize>(size));
    return words;
}

const char* descriptorTypeName(VkDescriptorType type)
{
    switch (type) {
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        return "storage buffer";
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        return "storage image";
    case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
        return "acceleration structure";
    default:
        return "other";
    }
}

// The descriptor type a reflected shader parameter needs.
VkDescriptorType descriptorTypeOf(const nlohmann::json& type, const std::string& where)
{
    const std::string shape = type.value("baseShape", "");
    if (shape == "structuredBuffer" || shape == "byteAddressBuffer") {
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    }
    if (shape == "accelerationStructure") {
        return VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    }
    if (shape.rfind("texture", 0) == 0 && type.value("access", "") == "readWrite") {
        return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    }
    throw std::runtime_error(where + ": resource kind '" + shape + "' is not supported by ComputeKernel");
}

// Compares the shader's own report of its interface with what the C++ side declares.
void checkAgainstReflection(const KernelDescription& description, const std::string& name)
{
    std::filesystem::path reportPath = description.spirvPath;
    reportPath.replace_extension(".json");
    std::ifstream file(reportPath);
    if (!file) {
        throw std::runtime_error("Missing shader reflection report '" + reportPath.string() +
                                 "'. Rebuild the shaders (target *_shaders).");
    }
    const nlohmann::json report = nlohmann::json::parse(file);

    std::uint32_t shaderPushSize = 0;
    for (const nlohmann::json& parameter : report.at("parameters")) {
        const std::string parameterName = parameter.value("name", "?");
        const std::string where = "Shader '" + name + "', parameter '" + parameterName + "'";
        const nlohmann::json& binding = parameter.at("binding");
        const std::string kind = binding.at("kind");

        if (kind == "pushConstantBuffer") {
            shaderPushSize = parameter.at("type").at("elementVarLayout").at("binding").at("size");
            continue;
        }
        if (kind != "descriptorTableSlot") {
            throw std::runtime_error(where + ": binding kind '" + kind + "' is not supported by ComputeKernel");
        }
        if (binding.value("space", 0) != 0) {
            throw std::runtime_error(where + ": only descriptor set 0 is supported");
        }

        const std::uint32_t index = binding.at("index");
        const VkDescriptorType expected = descriptorTypeOf(parameter.at("type"), where);
        const auto provided = std::find_if(description.bindings.begin(), description.bindings.end(),
                                           [&](const KernelBinding& b) { return b.binding == index; });
        if (provided == description.bindings.end()) {
            throw std::runtime_error(where + " reads binding " + std::to_string(index) +
                                     ", but the C++ side does not provide that binding");
        }
        if (provided->type != expected) {
            throw std::runtime_error(where + " at binding " + std::to_string(index) + " is a " +
                                     descriptorTypeName(expected) + ", but the C++ side provides a " +
                                     descriptorTypeName(provided->type));
        }
    }

    if (shaderPushSize != description.pushConstantSize) {
        throw std::runtime_error("Shader '" + name + "' expects " + std::to_string(shaderPushSize) +
                                 " bytes of push constants, but the C++ struct has " +
                                 std::to_string(description.pushConstantSize));
    }
}

} // namespace

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

ComputeKernel::ComputeKernel(const Context& context, const KernelDescription& description)
    : context_(context),
      name_(description.spirvPath.filename().string()),
      pushConstantSize_(description.pushConstantSize)
{
    checkAgainstReflection(description, name_);
    const VkDevice device = context.device();

    std::vector<VkDescriptorSetLayoutBinding> bindings;
    for (const KernelBinding& binding : description.bindings) {
        VkDescriptorSetLayoutBinding layoutBinding{};
        layoutBinding.binding = binding.binding;
        layoutBinding.descriptorType = binding.type;
        layoutBinding.descriptorCount = 1;
        layoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings.push_back(layoutBinding);
    }
    VkDescriptorSetLayoutCreateInfo setLayoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    setLayoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    setLayoutInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
    setLayoutInfo.pBindings = bindings.data();
    THESIS_VK_CHECK(vkCreateDescriptorSetLayout(device, &setLayoutInfo, nullptr, &setLayout_));

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
    readStatistics();
}

void ComputeKernel::readStatistics()
{
    if (!context_.pipelineStatistics()) {
        return;
    }
    // A compute pipeline has one executable: the compiled shader.
    VkPipelineExecutableInfoKHR executable{VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INFO_KHR};
    executable.pipeline = pipeline_;
    executable.executableIndex = 0;
    std::uint32_t count = 0;
    THESIS_VK_CHECK(context_.dispatch().getPipelineExecutableStatisticsKHR(&executable, &count, nullptr));
    std::vector<VkPipelineExecutableStatisticKHR> raw(count, {VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_STATISTIC_KHR});
    THESIS_VK_CHECK(context_.dispatch().getPipelineExecutableStatisticsKHR(&executable, &count, raw.data()));
    for (const VkPipelineExecutableStatisticKHR& statistic : raw) {
        double value = 0.0;
        switch (statistic.format) {
        case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_BOOL32_KHR:
            value = statistic.value.b32 ? 1.0 : 0.0;
            break;
        case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_INT64_KHR:
            value = static_cast<double>(statistic.value.i64);
            break;
        case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR:
            value = static_cast<double>(statistic.value.u64);
            break;
        case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_FLOAT64_KHR:
            value = statistic.value.f64;
            break;
        default:
            continue;
        }
        statistics_.push_back({statistic.name, statistic.description, value});
    }
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

    const std::size_t count = resources.entries_.size();
    std::vector<VkWriteDescriptorSet> writes(count);
    std::vector<VkDescriptorBufferInfo> bufferInfos(count);
    std::vector<VkDescriptorImageInfo> imageInfos(count);
    std::vector<VkWriteDescriptorSetAccelerationStructureKHR> structureInfos(count);

    for (std::size_t i = 0; i < count; ++i) {
        const KernelResources::Entry& entry = resources.entries_[i];
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
            throw std::logic_error("ComputeKernel: unsupported descriptor type");
        }
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    context_.dispatch().cmdPushDescriptorSetKHR(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_, 0,
                                                static_cast<std::uint32_t>(writes.size()), writes.data());
    if (pushConstantSize_ > 0) {
        vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, pushConstantSize_, pushConstants);
    }
    vkCmdDispatch(cmd, groupsX, groupsY, groupsZ);
}

} // namespace thesis::gpu
