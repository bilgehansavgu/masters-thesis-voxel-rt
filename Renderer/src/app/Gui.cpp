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
namespace {

// A plain tool look, as in profilers and debuggers: neutral greys, square corners, thin borders,
// and colour only where it carries meaning (the selection, a problem, the highlight of a ray).
void applyStyle()
{
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = {10, 8};
    style.FramePadding = {6, 3};
    style.ItemSpacing = {8, 5};
    style.ItemInnerSpacing = {5, 4};
    style.IndentSpacing = 14;
    style.ScrollbarSize = 12;
    style.WindowRounding = 0;
    style.ChildRounding = 0;
    style.FrameRounding = 2;
    style.PopupRounding = 2;
    style.GrabRounding = 2;
    style.TabRounding = 0;
    style.ScrollbarRounding = 0;
    style.WindowBorderSize = 1;
    style.FrameBorderSize = 1;
    style.TabBorderSize = 0;
    style.SeparatorTextBorderSize = 1;
    style.WindowMenuButtonPosition = ImGuiDir_None;

    const auto grey = [](float level, float alpha = 1.0f) { return ImVec4{level, level, level, alpha}; };
    const ImVec4 selection{0.24f, 0.32f, 0.42f, 1.0f}; // the one accent: a muted slate blue
    const ImVec4 selectionHover{0.29f, 0.38f, 0.49f, 1.0f};
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = grey(0.86f);
    colors[ImGuiCol_TextDisabled] = grey(0.56f);
    colors[ImGuiCol_WindowBg] = grey(0.13f);
    colors[ImGuiCol_ChildBg] = grey(0.13f);
    colors[ImGuiCol_PopupBg] = grey(0.11f);
    colors[ImGuiCol_MenuBarBg] = grey(0.09f);
    colors[ImGuiCol_Border] = grey(0.24f);
    colors[ImGuiCol_BorderShadow] = grey(0.0f, 0.0f);
    colors[ImGuiCol_FrameBg] = grey(0.18f);
    colors[ImGuiCol_FrameBgHovered] = grey(0.22f);
    colors[ImGuiCol_FrameBgActive] = grey(0.25f);
    colors[ImGuiCol_TitleBg] = grey(0.10f);
    colors[ImGuiCol_TitleBgActive] = grey(0.10f);
    colors[ImGuiCol_TitleBgCollapsed] = grey(0.10f);
    colors[ImGuiCol_ScrollbarBg] = grey(0.11f);
    colors[ImGuiCol_ScrollbarGrab] = grey(0.27f);
    colors[ImGuiCol_ScrollbarGrabHovered] = grey(0.33f);
    colors[ImGuiCol_ScrollbarGrabActive] = grey(0.38f);
    colors[ImGuiCol_CheckMark] = grey(0.86f);
    colors[ImGuiCol_SliderGrab] = grey(0.50f);
    colors[ImGuiCol_SliderGrabActive] = grey(0.62f);
    colors[ImGuiCol_Button] = grey(0.21f);
    colors[ImGuiCol_ButtonHovered] = grey(0.27f);
    colors[ImGuiCol_ButtonActive] = selection;
    colors[ImGuiCol_Header] = selection;
    colors[ImGuiCol_HeaderHovered] = selectionHover;
    colors[ImGuiCol_HeaderActive] = selectionHover;
    colors[ImGuiCol_Separator] = grey(0.24f);
    colors[ImGuiCol_SeparatorHovered] = grey(0.40f);
    colors[ImGuiCol_SeparatorActive] = grey(0.50f);
    colors[ImGuiCol_ResizeGrip] = grey(0.0f, 0.0f);
    colors[ImGuiCol_ResizeGripHovered] = grey(0.40f);
    colors[ImGuiCol_ResizeGripActive] = grey(0.50f);
    colors[ImGuiCol_Tab] = grey(0.11f);
    colors[ImGuiCol_TabHovered] = grey(0.22f);
    colors[ImGuiCol_TabSelected] = grey(0.17f);
    colors[ImGuiCol_TabSelectedOverline] = selectionHover;
    colors[ImGuiCol_TabDimmed] = grey(0.11f);
    colors[ImGuiCol_TabDimmedSelected] = grey(0.15f);
    colors[ImGuiCol_TabDimmedSelectedOverline] = grey(0.0f, 0.0f);
    colors[ImGuiCol_DockingPreview] = ImVec4{selection.x, selection.y, selection.z, 0.6f};
    colors[ImGuiCol_DockingEmptyBg] = grey(0.0f, 0.0f);
    colors[ImGuiCol_TableHeaderBg] = grey(0.16f);
    colors[ImGuiCol_TableBorderStrong] = grey(0.24f);
    colors[ImGuiCol_TableBorderLight] = grey(0.20f);
    colors[ImGuiCol_TableRowBg] = grey(0.0f, 0.0f);
    colors[ImGuiCol_TableRowBgAlt] = grey(1.0f, 0.025f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4{selection.x, selection.y, selection.z, 0.8f};
    colors[ImGuiCol_NavCursor] = selectionHover;
}

} // namespace

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
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // the dock layout is built by the panel each start, not loaded
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    applyStyle();
    // Use the system UI face where available; the embedded face also works on other installations.
    if (std::filesystem::is_regular_file(kInterfaceFontFile)) {
        io.Fonts->AddFontFromFileTTF(kInterfaceFontFile, 16.0f);
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
