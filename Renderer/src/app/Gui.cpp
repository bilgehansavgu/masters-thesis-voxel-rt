#include "Gui.h"

#include "Window.h"

#include "thesis/gpu/Check.h"
#include "thesis/gpu/Context.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace thesis::app {

Gui::Gui(const gpu::Context& context, const Window& window, VkFormat targetFormat, std::uint32_t imageCount)
    : device_(context.device()), targetFormat_(targetFormat)
{
    const VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 16;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    THESIS_VK_CHECK(vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_));

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowPadding = {14, 12};
    style.ItemSpacing = {8, 7};
    style.FramePadding = {8, 4};
    style.WindowRounding = 8;
    style.FrameRounding = 4;
    style.Colors[ImGuiCol_WindowBg] = {0.07f, 0.10f, 0.13f, 0.98f};
    style.Colors[ImGuiCol_TextDisabled] = {0.68f, 0.73f, 0.77f, 1};
    style.Colors[ImGuiCol_TitleBgActive] = {0.09f, 0.19f, 0.21f, 1};
    style.Colors[ImGuiCol_Button] = {0.10f, 0.34f, 0.37f, 1};
    style.Colors[ImGuiCol_ButtonHovered] = {0.13f, 0.44f, 0.47f, 1};
    // Use the system UI face where available; the embedded face also works on other installations.
    if (std::filesystem::is_regular_file(kInterfaceFontFile)) {
        ImGui::GetIO().Fonts->AddFontFromFileTTF(kInterfaceFontFile, 17.0f);
    }
    ImGui_ImplGlfw_InitForVulkan(window.handle(), true);

    ImGui_ImplVulkan_InitInfo info{};
    info.Instance = context.instance();
    info.PhysicalDevice = context.physicalDevice();
    info.Device = context.device();
    info.QueueFamily = context.queueFamily();
    info.Queue = context.queue();
    info.DescriptorPool = descriptorPool_;
    info.MinImageCount = 2;
    info.ImageCount = std::max(2u, imageCount);
    info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    info.UseDynamicRendering = true;
    info.PipelineRenderingCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &targetFormat_;
    info.CheckVkResultFn = [](VkResult result) {
        if (result < 0) {
            std::fprintf(stderr, "ImGui Vulkan backend: VkResult %d\n", static_cast<int>(result));
        }
    };
    ImGui_ImplVulkan_Init(&info);
}

Gui::~Gui()
{
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
}

void Gui::newFrame()
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void Gui::render(VkCommandBuffer cmd, VkImageView view, VkExtent2D extent)
{
    ImGui::Render();
    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = view;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea = {{0, 0}, extent};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color;
    vkCmdBeginRendering(cmd, &rendering);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    vkCmdEndRendering(cmd);
}

void Gui::swapchainRebuilt()
{
    ImGui_ImplVulkan_SetMinImageCount(2);
}

} // namespace thesis::app
