// The status bar: one line under everything with every setting of the frame on screen.

#include "ControlPanel.h"

#include "PanelWidgets.h"

#include "thesis/shared/RayOrder.h"

#include <imgui.h>

#include <format>

namespace thesis::app {
namespace {

std::string cameraName(const std::optional<std::array<float, 3>>& preset)
{
    if (!preset)
        return "free";
    for (const CameraPreset& known : kCameraPresets) {
        if (known.camera == *preset)
            return known.name;
    }
    return std::format("{} {} x{}", (*preset)[0], (*preset)[1], (*preset)[2]);
}

} // namespace

void ControlPanel::drawStatusBar()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float height = ImGui::GetFrameHeight();
    ImGui::SetNextWindowPos({viewport->WorkPos.x, viewport->WorkPos.y + viewport->WorkSize.y - height});
    ImGui::SetNextWindowSize({viewport->WorkSize.x, height});
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {ImGui::GetFontSize() * 0.5f, 2.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    if (ImGui::Begin("##status", nullptr, flags)) {
        const std::filesystem::path& model = facts_.models.at(facts_.model);
        std::string text = model.empty() ? std::string("box field") : model.filename().string();
        if (!facts_.encodings.empty()) {
            text +=
                "   " + facts_.encodingChoices.describe(facts_.encoding) + std::format("   Brick {}", facts_.brickSize);
        }
        text += std::format("   Rays {} {}x{}   Camera {}   {}   {}",
                            facts_.rayOrder == THESIS_RAY_ORDER_TILES ? "tiles" : "rows", facts_.width, facts_.height,
                            cameraName(cameraPreset_), tracerLabel(facts_.tracers.at(activeTracer_).name),
                            resolve::kShadingModeNames.at(static_cast<std::size_t>(shading_.mode)));
        text += std::format("   Trace {:.3f} ms{}", traceMs_.value, lastFrameCounted_ ? " (counting)" : "");
        if (!loading_.empty()) {
            ImGui::TextColored(kHighlightColor, "%s ...", loading_.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%s", text.c_str());
        } else {
            ImGui::TextUnformatted(text.c_str());
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

} // namespace thesis::app
