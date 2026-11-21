// The Settings window: every setting of the frame, in the order a ray meets them.

#include "ControlPanel.h"

#include "PanelWidgets.h"

#include "thesis/shared/RayOrder.h"

#include <glm/geometric.hpp>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace thesis::app {
namespace {

using resolve::ShadingMode;

std::string modelName(const std::filesystem::path& model)
{
    return model.empty() ? std::string("box field") : model.filename().string();
}

const char* rayOrderName(std::uint32_t order)
{
    return order == THESIS_RAY_ORDER_TILES ? "tiles 8x4" : "rows";
}

// A section of the settings: a header that stays open unless the user closes it.
bool section(const char* title)
{
    ImGui::Spacing();
    return sectionHeader(title);
}

} // namespace

void ControlPanel::drawSettings()
{
    if (!problem_.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kProblemColor, "%s", problem_.c_str());
        ImGui::PopTextWrapPos();
    }
    drawSceneSettings();
    drawEncodingSettings();
    drawRaySettings();
    drawSearchSettings();
    drawOutputSettings();
    drawKeys();
}

void ControlPanel::drawSceneSettings()
{
    if (!section("Scene"))
        return;
    const bool locked = measuring() || !loading_.empty();
    rowLabel("Model");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##model", modelName(facts_.models.at(wantedModel_)).c_str())) {
        for (std::size_t i = 0; i < facts_.models.size(); ++i) {
            if (ImGui::Selectable(modelName(facts_.models[i]).c_str(), i == wantedModel_) && !locked)
                selectModel(i);
        }
        ImGui::EndCombo();
    }
    ImGui::TextDisabled("%s", facts_.sceneDetails.c_str());
    if (facts_.voxelCount > 0)
        ImGui::TextDisabled("%llu voxels, %u bricks", static_cast<unsigned long long>(facts_.voxelCount),
                            facts_.primitiveCount);
    else
        ImGui::TextDisabled("%u boxes", facts_.primitiveCount);
}

void ControlPanel::drawEncodingSettings()
{
    if (facts_.encodings.empty() || !section("Encoding"))
        return;
    const bool locked = measuring() || !loading_.empty();
    // One switch per setting of the encodings (IEncoding::settings): the panel names no encoding.
    for (const encoding::EncodingChoices::Switch& option : facts_.encodingChoices.switches()) {
        rowLabel(option.setting.c_str());
        const std::string wanted = facts_.encodingChoices.valueOf(wantedEncoding_, option.setting);
        for (const std::string& value : option.values) {
            if (value != option.values.front())
                ImGui::SameLine();
            const std::string id = value + "##" + option.setting;
            if (ImGui::RadioButton(id.c_str(), wanted == value) && !locked)
                selectEncodingSetting(option.setting, value);
        }
    }
    if (!facts_.brickSizes.empty()) {
        rowLabel("Brick size");
        for (const std::uint32_t size : facts_.brickSizes) {
            if (size != facts_.brickSizes.front())
                ImGui::SameLine();
            if (ImGui::RadioButton(std::format("{}##brick", size).c_str(), wantedBrickSize_ == size) && !locked) {
                wantedBrickSize_ = size;
                problem_.clear();
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("voxels");
        helpMarker("Voxels along one side of a brick. A brick is one box for the search structure of a tracer. Key B.");
    }
    const PanelFacts::Encoding& shown = facts_.encodings.at(facts_.encoding);
    ImGui::TextDisabled("%s, %.2f bits/voxel, encoded in %.0f ms", formatBytes(facts_.sceneBytes).c_str(),
                        8.0 * static_cast<double>(facts_.sceneBytes) /
                            static_cast<double>(std::max<std::uint64_t>(facts_.voxelCount, 1)),
                        facts_.encodeMilliseconds);
    helpMarker((shown.name + ": " + shown.description +
                "\nSizes of the other encodings at this brick size are in Measurements, Memory.")
                   .c_str());
}

void ControlPanel::drawRaySettings()
{
    if (!section("Rays"))
        return;
    const bool locked = measuring() || !loading_.empty();
    rowLabel("Ray order");
    for (const std::uint32_t order : {THESIS_RAY_ORDER_ROWS, THESIS_RAY_ORDER_TILES}) {
        if (order != THESIS_RAY_ORDER_ROWS)
            ImGui::SameLine();
        if (ImGui::RadioButton(rayOrderName(order), wantedRayOrder_ == order) && !locked)
            selectRayOrder(order);
    }
    helpMarker("Where the ray of a pixel sits in the ray list. The GPU traces 32 neighbouring rays of the list "
               "together: 32 pixels of a row, or a tile of 8 x 4 pixels. Same image, other speed.");

    rowLabel("Camera");
    for (std::size_t i = 0; i < kCameraPresets.size(); ++i) {
        const CameraPreset& preset = kCameraPresets[i];
        if (i > 0)
            ImGui::SameLine();
        if (ImGui::RadioButton(std::format("{}##camera", preset.name).c_str(), cameraPreset_ == preset.camera))
            selectCameraPreset(i);
        ImGui::SetItemTooltip("yaw %.0f deg, pitch %.0f deg, distance x%g", preset.camera[0], preset.camera[1],
                              preset.camera[2]);
    }
    helpMarker("The cameras of the benchmark plan (--camera yaw pitch factor). The factor scales the distance "
               "that shows the whole scene. Moving the camera leaves the preset.");
    wrapped(std::format("{} x {} rays, yaw {:.1f} deg, pitch {:.1f} deg, distance {:.4g}", facts_.width, facts_.height,
                        camera_.yawDegrees, camera_.pitchDegrees, camera_.distance),
            true);
    if (ImGui::SmallButton("Reset camera (R)") && !measuring())
        returnCamera();
}

void ControlPanel::drawSearchSettings()
{
    if (!section("Search"))
        return;
    if (ImGui::BeginTable("tracers", 2, ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Tracer", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Structure", ImGuiTableColumnFlags_WidthFixed);
        for (std::size_t i = 0; i < facts_.tracers.size(); ++i) {
            const PanelFacts::Tracer& tracer = facts_.tracers[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Selectable(tracerLabel(tracer.name), i == activeTracer_, ImGuiSelectableFlags_SpanAllColumns) &&
                !measuring())
                selectTracer(i);
            if (ImGui::BeginItemTooltip()) {
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
                ImGui::Text("%s (--tracer %s)", tracerLabel(tracer.name), tracer.name.c_str());
                ImGui::TextDisabled("%s", tracer.method.empty() ? tracer.description.c_str() : tracer.method.c_str());
                if (tracer.buildMilliseconds > 0.0)
                    ImGui::TextDisabled("Build: %.1f ms on the %s", tracer.buildMilliseconds,
                                        tracer.buildOnCpu ? "CPU" : "GPU");
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", formatBytes(tracer.memoryBytes).c_str());
        }
        ImGui::EndTable();
    }
    for (const std::string& skipped : facts_.skippedTracers) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kProblemColor, "Not available: %s", skipped.c_str());
        ImGui::PopTextWrapPos();
    }
}

void ControlPanel::drawOutputSettings()
{
    if (!section("Output"))
        return;
    rowLabel("View");
    ImGui::SetNextItemWidth(-FLT_MIN);
    const ViewInfo* current = findView(shading_.mode);
    if (ImGui::BeginCombo("##view", current ? std::format("{}  {}", current->keyName, current->name).c_str() : "")) {
        for (const auto& [title, views] : {std::pair{"Hit", imageViews()}, std::pair{"Work per ray", workViews()}}) {
            ImGui::SeparatorText(title);
            for (const ViewInfo& view : views) {
                const char* reason = unavailableView(view.mode);
                ImGui::BeginDisabled(reason != nullptr);
                if (ImGui::Selectable(std::format("{}  {}", view.keyName, view.name).c_str(),
                                      shading_.mode == view.mode))
                    selectView(view.mode);
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("%s", reason ? reason : view.legend);
            }
        }
        ImGui::EndCombo();
    }
    if (current)
        helpMarker(!facts_.voxelCount && shading_.mode == ShadingMode::Shaded
                       ? "Light on the visible faces of the boxes."
                       : current->legend);

    drawLight();
    if (shading_.mode == ShadingMode::Distance) {
        rowLabel("Range");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::DragFloatRange2("##distance", &shading_.distanceNear, &shading_.distanceFar, 0.1f, 0.0f, 1000.0f,
                               "near %.1f", "far %.1f");
    }
    if (resolve::showsCount(shading_.mode)) {
        resolve::ColorScale& scale = shading_.workScale;
        rowLabel("Scale max");
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5.0f);
        ImGui::DragFloat("##scale max", &scale.maximum, 1.0f, 1.0f, 4096.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SetItemTooltip("The count at the end of the colour map. A larger count is white.");
        ImGui::SameLine();
        if (ImGui::RadioButton("linear", scale.kind == resolve::ScaleKind::Linear))
            scale.kind = resolve::ScaleKind::Linear;
        ImGui::SameLine();
        if (ImGui::RadioButton("log2", scale.kind == resolve::ScaleKind::Log2))
            scale.kind = resolve::ScaleKind::Log2;
        helpMarker("Viridis colour map: equal steps of the count look like equal steps of colour, lightness rises "
                   "with the count. log2: position on the map is log2(count) / log2(maximum). The bar is on the "
                   "image.");
    }
    if (resolve::showsWork(shading_.mode))
        ImGui::TextDisabled("Counting shader: its frames give no trace time.");
}

void ControlPanel::drawLight()
{
    if (!resolve::usesLight(shading_.mode))
        return;
    resolve::LightDirection& light = shading_.light;
    drawLightSphere();
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
    ImGui::SliderAngle("Azimuth", &light.azimuth, -180.0f, 180.0f, "%.0f deg");
    ImGui::SetItemTooltip("Angle of the light around the vertical axis (y). 0 deg: from +z. Ctrl + left-drag in the "
                          "image also moves the light.");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
    ImGui::SliderAngle("Elevation", &light.elevation, -90.0f, 90.0f, "%.0f deg");
    ImGui::SetItemTooltip("Angle of the light above the horizontal plane. 90 deg: straight from above.");
    if (ImGui::SmallButton("Reset light"))
        light = resolve::LightDirection{};
    ImGui::EndGroup();
}

void ControlPanel::drawLightSphere()
{
    // A sphere seen from the current camera, lit with the same rule and display curve as the
    // lit views (shade.slang). The dot marks the light: filled when the light is on the camera
    // side of the sphere, a ring when it is behind. A click or drag on the sphere puts the light
    // above the point under the mouse.
    resolve::LightDirection& light = shading_.light;
    const core::CameraFrame frame = camera_.frame(1.0f);
    const glm::vec3 right = glm::normalize(frame.right);
    const glm::vec3 up = glm::normalize(frame.up);
    const glm::vec3 back = -glm::normalize(frame.forward); // towards the viewer
    const float horizontal = std::cos(light.elevation);
    const glm::vec3 toLight{horizontal * std::sin(light.azimuth), std::sin(light.elevation),
                            horizontal * std::cos(light.azimuth)};
    const glm::vec3 lightInView{glm::dot(toLight, right), glm::dot(toLight, up), glm::dot(toLight, back)};

    const float size = ImGui::GetFrameHeight() * 3.0f + 2.0f * ImGui::GetStyle().ItemSpacing.y;
    const ImVec2 corner = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##light sphere", ImVec2(size, size));
    const float radius = 0.5f * size - 1.0f;
    const ImVec2 centre(corner.x + 0.5f * size, corner.y + 0.5f * size);

    if (ImGui::IsItemActive()) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        float x = (mouse.x - centre.x) / radius;
        float y = (centre.y - mouse.y) / radius;
        const float length = std::sqrt(x * x + y * y);
        if (length > 1.0f) {
            x /= length;
            y /= length;
        }
        const float z = std::sqrt(std::max(0.0f, 1.0f - x * x - y * y));
        const glm::vec3 world = glm::normalize(x * right + y * up + z * back);
        light.azimuth = std::atan2(world.x, world.z);
        light.elevation = std::asin(std::clamp(world.y, -1.0f, 1.0f));
    }
    ImGui::SetItemTooltip("The light on a sphere seen from the camera. Filled dot: in front, ring: behind. Click or "
                          "drag to place the light.");

    ImDrawList* draw = ImGui::GetWindowDrawList();
    constexpr int kCells = 28;
    const float cell = 2.0f * radius / kCells;
    for (int row = 0; row < kCells; ++row) {
        for (int column = 0; column < kCells; ++column) {
            const float x = -1.0f + (column + 0.5f) * 2.0f / kCells;
            const float y = 1.0f - (row + 0.5f) * 2.0f / kCells;
            const float rr = x * x + y * y;
            if (rr > 1.0f)
                continue;
            const glm::vec3 normal{x, y, std::sqrt(1.0f - rr)};
            const float linear = 0.8f * (0.25f + 0.75f * std::max(glm::dot(normal, lightInView), 0.0f));
            const auto grey = static_cast<int>(255.0f * std::pow(linear, 1.0f / 2.2f) + 0.5f);
            const ImVec2 min(centre.x - radius + column * cell, centre.y - radius + row * cell);
            draw->AddRectFilled(min, ImVec2(min.x + cell + 0.5f, min.y + cell + 0.5f), IM_COL32(grey, grey, grey, 255));
        }
    }
    draw->AddCircle(centre, radius, ImGui::GetColorU32(ImGuiCol_Border), 48, 1.0f);

    const ImVec2 dot(centre.x + lightInView.x * radius, centre.y - lightInView.y * radius);
    const ImU32 yellow = ImGui::ColorConvertFloat4ToU32(kHighlightColor);
    if (lightInView.z >= 0.0f) {
        draw->AddCircleFilled(dot, 4.0f, yellow);
        draw->AddCircle(dot, 4.0f, IM_COL32(40, 30, 0, 255), 12, 1.0f);
    } else {
        draw->AddCircle(dot, 4.0f, yellow, 12, 2.0f);
    }
}

void ControlPanel::drawKeys()
{
    ImGui::Spacing();
    if (!sectionHeader("Keys and mouse", false))
        return;
    static constexpr std::pair<const char*, const char*> kKeys[] = {
        {"Left drag", "orbit"},
        {"Ctrl + left drag", "move the light"},
        {"Wheel", "zoom"},
        {"Right drag", "fly: look; W/A/S/D move, Q/E down/up, Shift x4, wheel speed"},
        {"Right click", "pick the pixel under the mouse"},
        {"Left / Right", "previous / next step of the ray walk"},
        {"V", "ray from the side"},
        {"S", "search structure"},
        {"Esc", "let the ray go"},
        {"Tab, E, B", "next tracer, encoding, brick size (Shift: previous)"},
        {"1-9, 0, -, =", "views"},
        {"M", "quick timing"},
        {"R", "reset camera"},
    };
    if (ImGui::BeginTable("keys", 2, ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("key", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("action", ImGuiTableColumnFlags_WidthStretch);
        for (const auto& [key, action] : kKeys) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(key);
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", action);
        }
        ImGui::EndTable();
    }
}

} // namespace thesis::app
