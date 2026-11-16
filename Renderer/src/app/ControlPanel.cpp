#include "ControlPanel.h"

#include "FramePipeline.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <iterator>
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

ControlPanel::ControlPanel(PanelFacts facts, const core::OrbitCamera& initialCamera, std::size_t initialTracer,
                           std::size_t initialEncoding)
    : facts_(std::move(facts)),
      initialCamera_(initialCamera),
      camera_(initialCamera),
      activeTracer_(initialTracer),
      activeEncoding_(initialEncoding)
{
    shading_.distanceFar = 2.0f * initialCamera.distance;
}

void ControlPanel::setFacts(PanelFacts facts)
{
    const std::string previous = facts_.tracers.at(activeTracer_).name;
    facts_ = std::move(facts);
    const auto same = std::find_if(facts_.tracers.begin(), facts_.tracers.end(),
                                   [&](const PanelFacts::Tracer& tracer) { return tracer.name == previous; });
    activeTracer_ = same == facts_.tracers.end() ? 0 : static_cast<std::size_t>(same - facts_.tracers.begin());
    traceMs_ = {}; // the old average belongs to the old scene
}

void ControlPanel::selectNextEncoding()
{
    if (!facts_.encodings.empty()) {
        activeEncoding_ = (activeEncoding_ + 1) % facts_.encodings.size();
    }
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
        if (ImGui::IsKeyPressed(ImGuiKey_E)) {
            selectNextEncoding();
        }
        const ImGuiKey modeKeys[] = {ImGuiKey_1, ImGuiKey_2, ImGuiKey_3, ImGuiKey_4, ImGuiKey_5,
                                     ImGuiKey_6, ImGuiKey_7, ImGuiKey_8, ImGuiKey_9};
        static_assert(std::size(modeKeys) == resolve::kShadingModeNames.size());
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
    ImGui::SetNextWindowSize({400, 0}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Renderer");

    if (ImGui::BeginCombo("Tracer (Tab)", tracer.name.c_str())) {
        for (std::size_t i = 0; i < facts_.tracers.size(); ++i) {
            if (ImGui::Selectable(facts_.tracers[i].name.c_str(), i == activeTracer_)) {
                activeTracer_ = i;
            }
        }
        ImGui::EndCombo();
    }
    for (const std::string& skipped : facts_.skippedTracers) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Not available: %s", skipped.c_str());
        ImGui::PopTextWrapPos();
    }
    if (!facts_.encodings.empty()) {
        // The scene shown belongs to the encoding in facts_; the choice takes effect next frame.
        if (ImGui::BeginCombo("Encoding (E)", facts_.encodings.at(activeEncoding_).c_str())) {
            for (std::size_t i = 0; i < facts_.encodings.size(); ++i) {
                if (ImGui::Selectable(facts_.encodings[i].c_str(), i == activeEncoding_)) {
                    activeEncoding_ = i;
                }
            }
            ImGui::EndCombo();
        }
    }
    int mode = static_cast<int>(shading_.mode);
    if (ImGui::Combo("View (1-9)", &mode, resolve::kShadingModeNames.data(),
                     static_cast<int>(resolve::kShadingModeNames.size()))) {
        shading_.mode = static_cast<resolve::ShadingMode>(mode);
    }
    if (shading_.mode == resolve::ShadingMode::Distance) {
        ImGui::DragFloatRange2("Distance range", &shading_.distanceNear, &shading_.distanceFar, 0.1f, 0.0f, 1000.0f);
    }
    const bool workView = resolve::showsWork(shading_.mode);
    if (workView && shading_.mode != resolve::ShadingMode::WorkHitCell) {
        ImGui::DragFloat("Count at red", &shading_.workScale, 1.0f, 1.0f, 4096.0f, "%.0f");
        ImGui::TextDisabled("black 0, blue, green, yellow, red, white above");
    }
    if (shading_.mode == resolve::ShadingMode::WorkHitCell) {
        ImGui::TextDisabled("green: one voxel, blue: one run, red: several runs");
    }
    if (workView && !tracer.countsWork) {
        ImGui::TextDisabled("This tracer does not count work.");
    }

    ImGui::SeparatorText("GPU time per frame");
    const double rays = static_cast<double>(facts_.width) * facts_.height;
    const double megaRays = traceMs_.value > 0.0 ? rays / (traceMs_.value * 1e3) : 0.0;
    ImGui::Text("ray generation %8.3f ms", rayGenerationMs_.value);
    ImGui::Text("trace          %8.3f ms  (%.0f Mrays/s)", traceMs_.value, megaRays);
    ImGui::Text("shading        %8.3f ms", shadingMs_.value);
    if (workView && tracer.countsWork) {
        ImGui::TextDisabled("A work view runs the counting shader: slower than the tracer.");
    } else {
        ImGui::TextDisabled("Live numbers. Use --benchmark for thesis numbers.");
    }

    ImGui::SeparatorText("Scene");
    ImGui::Text("%s", facts_.sceneName.c_str());
    ImGui::Text("%s", facts_.sceneDetails.c_str());
    if (facts_.voxelCount > 0) {
        ImGui::Text("%llu voxels in %u bricks", static_cast<unsigned long long>(facts_.voxelCount),
                    facts_.primitiveCount);
    } else {
        ImGui::Text("%u primitives", facts_.primitiveCount);
    }
    ImGui::Text("%ux%u rays", facts_.width, facts_.height);

    ImGui::SeparatorText("Scene memory on the GPU");
    for (const PanelFacts::SceneBuffer& buffer : facts_.sceneBuffers) {
        ImGui::Text("%-16s %s", buffer.name.c_str(), formatBytes(buffer.bytes).c_str());
    }
    if (facts_.voxelCount > 0) {
        ImGui::Text("%-16s %s  (%.2f bits per voxel)", "total", formatBytes(facts_.sceneBytes).c_str(),
                    8.0 * static_cast<double>(facts_.sceneBytes) / static_cast<double>(facts_.voxelCount));
        ImGui::Text("%-16s %.1f ms", "encode time", facts_.encodeMilliseconds);
    } else {
        ImGui::Text("%-16s %s", "total", formatBytes(facts_.sceneBytes).c_str());
    }

    ImGui::SeparatorText("Tracer memory");
    ImGui::Text("%-16s %s", "search structure", formatBytes(tracer.memoryBytes).c_str());
    ImGui::Text("%-16s %s", "rays + hits", formatBytes(facts_.rayAndHitBytes).c_str());
    ImGui::Text("%-16s %.3f ms (%s)", "build time", tracer.buildMilliseconds, tracer.buildOnCpu ? "CPU" : "GPU");

    ImGui::SeparatorText("Camera");
    ImGui::TextDisabled("Drag to orbit, scroll to zoom");
    if (ImGui::Button("Reset camera")) {
        camera_ = initialCamera_;
    }
    ImGui::End();
}

} // namespace thesis::app
