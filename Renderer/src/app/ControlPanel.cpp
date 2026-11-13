#include "ControlPanel.h"

#include "FramePipeline.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <string_view>
#include <utility>

namespace thesis::app {
namespace {

std::string formatBytes(std::uint64_t bytes)
{
    char buffer[32];
    if (bytes >= (1ull << 20)) {
        std::snprintf(buffer, sizeof(buffer), "%.1f MiB", static_cast<double>(bytes) / (1 << 20));
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.1f KiB", static_cast<double>(bytes) / (1 << 10));
    }
    return buffer;
}

} // namespace

ControlPanel::ControlPanel(PanelFacts facts, const core::OrbitCamera& initialCamera, std::size_t initialTracer)
    : facts_(std::move(facts)), initialCamera_(initialCamera), camera_(initialCamera), activeTracer_(initialTracer)
{
    shading_.distanceFar = 2.0f * initialCamera.distance;
}

void ControlPanel::handleInput()
{
    const ImGuiIO& io = ImGui::GetIO();
    if (!io.WantCaptureMouse) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            camera_.yawDegrees -= 0.25f * io.MouseDelta.x;
            camera_.pitchDegrees = std::clamp(camera_.pitchDegrees + 0.25f * io.MouseDelta.y, -89.0f, 89.0f);
        }
        if (io.MouseWheel != 0.0f) {
            camera_.distance = std::max(0.1f, camera_.distance * (1.0f - 0.1f * io.MouseWheel));
        }
    }
    if (!io.WantCaptureKeyboard) {
        if (ImGui::IsKeyPressed(ImGuiKey_Tab) && !facts_.tracers.empty()) {
            activeTracer_ = (activeTracer_ + 1) % facts_.tracers.size();
        }
        const ImGuiKey modeKeys[] = {ImGuiKey_1, ImGuiKey_2, ImGuiKey_3, ImGuiKey_4};
        for (std::size_t mode = 0; mode < std::size(modeKeys); ++mode) {
            if (ImGui::IsKeyPressed(modeKeys[mode])) {
                shading_.mode = static_cast<resolve::ShadingMode>(mode);
            }
        }
    }
}

void ControlPanel::recordTimings(const std::vector<gpu::StageTiming>& timings)
{
    for (const gpu::StageTiming& timing : timings) {
        const std::string_view stage = timing.stage;
        if (stage == kStageRayGeneration) {
            rayGenerationMs_.add(timing.milliseconds);
        } else if (stage == kStageTrace) {
            traceMs_.add(timing.milliseconds);
        } else if (stage == kStageShading) {
            shadingMs_.add(timing.milliseconds);
        }
    }
}

void ControlPanel::draw()
{
    const PanelFacts::Tracer& tracer = facts_.tracers.at(activeTracer_);

    ImGui::SetNextWindowPos({10, 10}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({360, 0}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Renderer");

    if (ImGui::BeginCombo("Tracer (Tab)", tracer.name.c_str())) {
        for (std::size_t i = 0; i < facts_.tracers.size(); ++i) {
            if (ImGui::Selectable(facts_.tracers[i].name.c_str(), i == activeTracer_)) {
                activeTracer_ = i;
            }
        }
        ImGui::EndCombo();
    }
    int mode = static_cast<int>(shading_.mode);
    if (ImGui::Combo("View (1-4)", &mode, resolve::kShadingModeNames.data(),
                     static_cast<int>(resolve::kShadingModeNames.size()))) {
        shading_.mode = static_cast<resolve::ShadingMode>(mode);
    }
    if (shading_.mode == resolve::ShadingMode::Distance) {
        ImGui::DragFloatRange2("Distance range", &shading_.distanceNear, &shading_.distanceFar, 0.1f, 0.0f, 1000.0f);
    }

    ImGui::SeparatorText("GPU time per frame");
    const double rays = static_cast<double>(facts_.width) * facts_.height;
    const double megaRays = traceMs_.value > 0.0 ? rays / (traceMs_.value * 1e3) : 0.0;
    ImGui::Text("ray generation %8.3f ms", rayGenerationMs_.value);
    ImGui::Text("trace          %8.3f ms  (%.0f Mrays/s)", traceMs_.value, megaRays);
    ImGui::Text("shading        %8.3f ms", shadingMs_.value);
    ImGui::TextDisabled("Live numbers. Use --benchmark for thesis numbers.");

    ImGui::SeparatorText("Scene");
    ImGui::Text("%s", facts_.sceneName.c_str());
    ImGui::Text("%u primitives, %ux%u rays", facts_.primitiveCount, facts_.width, facts_.height);

    ImGui::SeparatorText("Memory");
    ImGui::Text("scene            %s", formatBytes(facts_.sceneBytes).c_str());
    ImGui::Text("search structure %s", formatBytes(tracer.memoryBytes).c_str());
    ImGui::Text("rays + hits      %s", formatBytes(facts_.rayAndHitBytes).c_str());
    ImGui::Text("build time       %.3f ms", tracer.buildMilliseconds);

    ImGui::SeparatorText("Camera");
    ImGui::TextDisabled("Drag to orbit, scroll to zoom");
    if (ImGui::Button("Reset camera")) {
        camera_ = initialCamera_;
    }
    ImGui::End();
}

} // namespace thesis::app
