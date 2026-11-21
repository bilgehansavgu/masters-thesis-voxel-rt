#include "ShaderInterface.h"

#include "thesis/gpu/Check.h"
#include "thesis/gpu/Context.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace thesis::gpu {
namespace {

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
    throw std::runtime_error(where + ": resource kind '" + shape + "' is not supported");
}

} // namespace

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

void checkAgainstReflection(const std::filesystem::path& spirvPath, const std::vector<KernelBinding>& bindings,
                            std::uint32_t pushConstantSize, const std::string& name)
{
    std::filesystem::path reportPath = spirvPath;
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
            throw std::runtime_error(where + ": binding kind '" + kind + "' is not supported");
        }
        if (binding.value("space", 0) != 0) {
            throw std::runtime_error(where + ": only descriptor set 0 is supported");
        }

        const std::uint32_t index = binding.at("index");
        const VkDescriptorType expected = descriptorTypeOf(parameter.at("type"), where);
        const auto provided =
            std::find_if(bindings.begin(), bindings.end(), [&](const KernelBinding& b) { return b.binding == index; });
        if (provided == bindings.end()) {
            throw std::runtime_error(where + " reads binding " + std::to_string(index) +
                                     ", but the C++ side does not provide that binding");
        }
        if (provided->type != expected) {
            throw std::runtime_error(where + " at binding " + std::to_string(index) + " is a " +
                                     descriptorTypeName(expected) + ", but the C++ side provides a " +
                                     descriptorTypeName(provided->type));
        }
    }

    if (shaderPushSize != pushConstantSize) {
        throw std::runtime_error("Shader '" + name + "' expects " + std::to_string(shaderPushSize) +
                                 " bytes of push constants, but the C++ struct has " +
                                 std::to_string(pushConstantSize));
    }
}

VkDescriptorSetLayout createPushDescriptorSetLayout(VkDevice device, const std::vector<KernelBinding>& bindings,
                                                    VkShaderStageFlags stages)
{
    std::vector<VkDescriptorSetLayoutBinding> layoutBindings;
    for (const KernelBinding& binding : bindings) {
        VkDescriptorSetLayoutBinding layoutBinding{};
        layoutBinding.binding = binding.binding;
        layoutBinding.descriptorType = binding.type;
        layoutBinding.descriptorCount = 1;
        layoutBinding.stageFlags = stages;
        layoutBindings.push_back(layoutBinding);
    }
    VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    info.bindingCount = static_cast<std::uint32_t>(layoutBindings.size());
    info.pBindings = layoutBindings.data();
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    THESIS_VK_CHECK(vkCreateDescriptorSetLayout(device, &info, nullptr, &layout));
    return layout;
}

std::vector<ShaderStatistic> readPipelineStatistics(const Context& context, VkPipeline pipeline)
{
    std::vector<ShaderStatistic> statistics;
    if (!context.pipelineStatistics()) {
        return statistics;
    }
    VkPipelineInfoKHR pipelineInfo{VK_STRUCTURE_TYPE_PIPELINE_INFO_KHR};
    pipelineInfo.pipeline = pipeline;
    std::uint32_t executableCount = 0;
    THESIS_VK_CHECK(context.dispatch().getPipelineExecutablePropertiesKHR(&pipelineInfo, &executableCount, nullptr));
    std::vector<VkPipelineExecutablePropertiesKHR> executables(executableCount,
                                                               {VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_PROPERTIES_KHR});
    THESIS_VK_CHECK(
        context.dispatch().getPipelineExecutablePropertiesKHR(&pipelineInfo, &executableCount, executables.data()));

    for (std::uint32_t index = 0; index < executableCount; ++index) {
        const std::string prefix = executableCount > 1 ? std::string(executables[index].name) + ": " : "";
        VkPipelineExecutableInfoKHR executable{VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INFO_KHR};
        executable.pipeline = pipeline;
        executable.executableIndex = index;
        std::uint32_t count = 0;
        THESIS_VK_CHECK(context.dispatch().getPipelineExecutableStatisticsKHR(&executable, &count, nullptr));
        std::vector<VkPipelineExecutableStatisticKHR> raw(count, {VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_STATISTIC_KHR});
        THESIS_VK_CHECK(context.dispatch().getPipelineExecutableStatisticsKHR(&executable, &count, raw.data()));
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
            statistics.push_back({prefix + statistic.name, statistic.description, value});
        }
    }
    return statistics;
}

} // namespace thesis::gpu
