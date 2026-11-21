#include "thesis/gpu/Context.h"

#include "thesis/gpu/Check.h"

#include <atomic>
#include <cstdio>
#include <stdexcept>

namespace thesis::gpu {
namespace {

std::atomic<std::uint64_t> gValidationMessages{0};

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                             VkDebugUtilsMessageTypeFlagsEXT /*types*/,
                                             const VkDebugUtilsMessengerCallbackDataEXT* data, void* /*user*/)
{
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        ++gValidationMessages;
        const char* label = severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ? "error" : "warning";
        std::fprintf(stderr, "[vulkan %s] %s\n", label, data->pMessage);
    }
    return VK_FALSE;
}

template <typename T>
T unwrap(vkb::Result<T> result, const char* what)
{
    if (!result) {
        throw std::runtime_error(std::string(what) + ": " + result.error().message());
    }
    return result.value();
}

} // namespace

std::uint64_t validationMessageCount()
{
    return gValidationMessages.load();
}

Context::Context(const ContextOptions& options)
{
    vkb::InstanceBuilder instanceBuilder;
    instanceBuilder.set_app_name(options.applicationName.c_str())
        .set_engine_name("thesis")
        .require_api_version(1, 3, 0);
    if (options.validation) {
        instanceBuilder.request_validation_layers(true)
            .set_debug_callback(debugCallback)
            .set_debug_messenger_severity(VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT);
    }
    for (const std::string& extension : options.instanceExtensions) {
        instanceBuilder.enable_extension(extension.c_str());
    }
    // Debug labels name the passes of a frame in Nsight Graphics and RenderDoc. They cost nothing
    // measurable, so they are on in every run where the loader offers them, not only with validation.
    const auto systemInfo = vkb::SystemInfo::get_system_info();
    const bool debugUtils = systemInfo && systemInfo->is_extension_available(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (debugUtils && !options.validation) {
        instanceBuilder.enable_extension(
            VK_EXT_DEBUG_UTILS_EXTENSION_NAME); // with validation, the messenger enables it
    }
    if (!options.createSurface) {
        instanceBuilder.set_headless(true);
    }
    instance_ = unwrap(instanceBuilder.build(), "Creating the Vulkan instance");
    if (debugUtils) {
        beginLabel_ = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(
            vkGetInstanceProcAddr(instance_.instance, "vkCmdBeginDebugUtilsLabelEXT"));
        endLabel_ = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(
            vkGetInstanceProcAddr(instance_.instance, "vkCmdEndDebugUtilsLabelEXT"));
    }

    if (options.createSurface) {
        surface_ = options.createSurface(instance_.instance);
    }

    VkPhysicalDeviceVulkan12Features features12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    features12.bufferDeviceAddress = VK_TRUE;
    VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    features13.synchronization2 = VK_TRUE;
    features13.dynamicRendering = VK_TRUE;
    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    accelerationStructureFeatures.accelerationStructure = VK_TRUE;
    VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
    rayQueryFeatures.rayQuery = VK_TRUE;

    // Requiring ray tracing extensions also rules out the integrated GPU of a hybrid laptop.
    vkb::PhysicalDeviceSelector selector(instance_);
    selector.set_minimum_version(1, 3)
        .prefer_gpu_device_type(vkb::PreferredDeviceType::discrete)
        .add_required_extension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)
        .add_required_extension(VK_KHR_RAY_QUERY_EXTENSION_NAME)
        .add_required_extension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME)
        .add_required_extension(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME)
        .set_required_features_12(features12)
        .set_required_features_13(features13)
        .add_required_extension_features(accelerationStructureFeatures)
        .add_required_extension_features(rayQueryFeatures);
    if (surface_ != VK_NULL_HANDLE) {
        selector.set_surface(surface_);
    } else {
        selector.require_present(false);
    }
    physicalDevice_ = unwrap(selector.select(), "Selecting a GPU with hardware ray tracing");
    memoryBudget_ = physicalDevice_.enable_extension_if_present(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
    // The driver's report on each compiled shader, for example how many registers one thread
    // uses. It is only read, never needed, so a driver without it is fine.
    VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR executableFeatures{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR};
    executableFeatures.pipelineExecutableInfo = VK_TRUE;
    pipelineStatistics_ =
        physicalDevice_.enable_extension_if_present(VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME) &&
        physicalDevice_.enable_extension_features_if_present(executableFeatures);
    // Ray tracing pipelines and shader execution reordering: optional, because only the tracers
    // built on them need them. Such a tracer checks rayTracingPipelines() and invocationReorder()
    // and refuses the scene (UnsupportedScene) when they are off.
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR pipelineFeatures{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
    pipelineFeatures.rayTracingPipeline = VK_TRUE;
    rayTracingPipelines_ = physicalDevice_.enable_extension_if_present(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) &&
                           physicalDevice_.enable_extension_features_if_present(pipelineFeatures);
    // WHY the NV extension and not the cross-vendor EXT one: the Vulkan headers of this build
    // (vcpkg, header version 309) only know the NV one.
    VkPhysicalDeviceRayTracingInvocationReorderFeaturesNV reorderFeatures{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_INVOCATION_REORDER_FEATURES_NV};
    reorderFeatures.rayTracingInvocationReorder = VK_TRUE;
    // queryDeviceInfo() turns this off again if the driver says that it does not reorder.
    invocationReorder_ =
        rayTracingPipelines_ &&
        physicalDevice_.enable_extension_if_present(VK_NV_RAY_TRACING_INVOCATION_REORDER_EXTENSION_NAME) &&
        physicalDevice_.enable_extension_features_if_present(reorderFeatures);

    device_ = unwrap(vkb::DeviceBuilder(physicalDevice_).build(), "Creating the Vulkan device");
    dispatch_ = device_.make_table();
    queue_ = unwrap(device_.get_queue(vkb::QueueType::graphics), "Getting the graphics queue");
    queueFamily_ = unwrap(device_.get_queue_index(vkb::QueueType::graphics), "Getting the queue family");

    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    if (memoryBudget_) {
        allocatorInfo.flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
    }
    allocatorInfo.instance = instance();
    allocatorInfo.physicalDevice = physicalDevice();
    allocatorInfo.device = device();
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    THESIS_VK_CHECK(vmaCreateAllocator(&allocatorInfo, &allocator_));

    queryDeviceInfo();

    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamily_;
    THESIS_VK_CHECK(vkCreateCommandPool(device(), &poolInfo, nullptr, &commandPool_));

    VkCommandBufferAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocateInfo.commandPool = commandPool_;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = 1;
    THESIS_VK_CHECK(vkAllocateCommandBuffers(device(), &allocateInfo, &commandBuffer_));

    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    THESIS_VK_CHECK(vkCreateFence(device(), &fenceInfo, nullptr, &fence_));

    tracy_ = TracyVkContext(physicalDevice(), device(), queue_, commandBuffer_);
}

Context::~Context()
{
    if (device() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device());
    }
    TracyVkDestroy(tracy_);
    if (fence_ != VK_NULL_HANDLE) {
        vkDestroyFence(device(), fence_, nullptr);
    }
    if (commandPool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device(), commandPool_, nullptr);
    }
    if (allocator_ != VK_NULL_HANDLE) {
        vmaDestroyAllocator(allocator_);
    }
    vkb::destroy_device(device_);
    if (surface_ != VK_NULL_HANDLE) {
        vkb::destroy_surface(instance_, surface_);
    }
    vkb::destroy_instance(instance_);
}

void Context::queryDeviceInfo()
{
    VkPhysicalDeviceAccelerationStructurePropertiesKHR accelerationProperties{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
    VkPhysicalDeviceVulkan12Properties properties12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES};
    properties12.pNext = &accelerationProperties;
    // The structs of optional extensions go into the chain only when the extension is on.
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR pipelineProperties{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};
    VkPhysicalDeviceRayTracingInvocationReorderPropertiesNV reorderProperties{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_INVOCATION_REORDER_PROPERTIES_NV};
    if (rayTracingPipelines_) {
        accelerationProperties.pNext = &pipelineProperties;
    }
    if (invocationReorder_) {
        pipelineProperties.pNext = &reorderProperties;
    }
    VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    properties.pNext = &properties12;
    vkGetPhysicalDeviceProperties2(physicalDevice(), &properties);

    info_.shaderGroupHandleSize = pipelineProperties.shaderGroupHandleSize;
    info_.shaderGroupHandleAlignment = pipelineProperties.shaderGroupHandleAlignment;
    info_.shaderGroupBaseAlignment = pipelineProperties.shaderGroupBaseAlignment;
    invocationReorder_ = invocationReorder_ && reorderProperties.rayTracingInvocationReorderReorderingHint ==
                                                   VK_RAY_TRACING_INVOCATION_REORDER_MODE_REORDER_NV;

    info_.name = properties.properties.deviceName;
    info_.driverName = properties12.driverName;
    info_.driverInfo = properties12.driverInfo;
    info_.vendorId = properties.properties.vendorID;
    info_.deviceId = properties.properties.deviceID;
    info_.apiVersion = properties.properties.apiVersion;
    info_.timestampPeriodNs = properties.properties.limits.timestampPeriod;
    info_.minScratchOffsetAlignment = accelerationProperties.minAccelerationStructureScratchOffsetAlignment;

    std::uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice(), &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice(), &familyCount, families.data());
    info_.timestampValidBits = families.at(queueFamily_).timestampValidBits;
    if (info_.timestampValidBits == 0) {
        throw std::runtime_error("The selected queue does not support GPU timestamps");
    }

    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice(), &memory);
    for (std::uint32_t i = 0; i < memory.memoryHeapCount; ++i) {
        if (memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
            info_.deviceLocalBytes += memory.memoryHeaps[i].size;
        }
    }
}

void Context::beginLabel(VkCommandBuffer cmd, const char* name) const
{
    if (beginLabel_ != nullptr) {
        VkDebugUtilsLabelEXT label{VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
        label.pLabelName = name;
        beginLabel_(cmd, &label);
    }
}

void Context::endLabel(VkCommandBuffer cmd) const
{
    if (endLabel_ != nullptr) {
        endLabel_(cmd);
    }
}

void Context::submitAndWait(const std::function<void(VkCommandBuffer)>& record) const
{
    THESIS_VK_CHECK(vkResetFences(device(), 1, &fence_));
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    THESIS_VK_CHECK(vkBeginCommandBuffer(commandBuffer_, &beginInfo));
    record(commandBuffer_);
    THESIS_VK_CHECK(vkEndCommandBuffer(commandBuffer_));

    VkCommandBufferSubmitInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    commandInfo.commandBuffer = commandBuffer_;
    VkSubmitInfo2 submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &commandInfo;
    THESIS_VK_CHECK(vkQueueSubmit2(queue_, 1, &submitInfo, fence_));
    THESIS_VK_CHECK(vkWaitForFences(device(), 1, &fence_, VK_TRUE, UINT64_MAX));
}

VkDeviceSize Context::deviceLocalUsage() const
{
    VmaBudget budgets[VK_MAX_MEMORY_HEAPS]{};
    vmaGetHeapBudgets(allocator_, budgets);
    const VkPhysicalDeviceMemoryProperties* memory = nullptr;
    vmaGetMemoryProperties(allocator_, &memory);
    VkDeviceSize used = 0;
    for (std::uint32_t i = 0; i < memory->memoryHeapCount; ++i) {
        if (memory->memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
            used += budgets[i].usage;
        }
    }
    return used;
}

} // namespace thesis::gpu
