// The Settings window: every setting of the frame, in the order a ray meets them.

#include "ControlPanel.h"

#include "FileDialog.h"
#include "PanelWidgets.h"
#include "ShellFiles.h"

#include "thesis/mesh/TriangleMesh.h"
#include "thesis/shared/RayOrder.h"

#include <glm/geometric.hpp>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <system_error>

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

// The resolutions "Import mesh" offers in its list: multiples of every brick size.
constexpr std::array<std::uint32_t, 7> kImportResolutions{128, 256, 512, 1024, 2048, 4096, 8192};
// A typed resolution: a multiple of the largest brick size, so every brick size divides the grid.
constexpr std::uint32_t kResolutionStep = 32;
constexpr std::uint32_t kMaxResolution = 16384;

// ImGui text is UTF-8; a path of the file system may hold any character.
std::string utf8(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

// The path typed or pasted into a text field. Surrounding spaces and quotes go: Windows
// "Copy as path" puts the path in quotes.
std::filesystem::path pathFromField(std::string_view text)
{
    const auto isTrimmed = [](char c) { return c == ' ' || c == '\t' || c == '"' || c == '\''; };
    while (!text.empty() && isTrimmed(text.front()))
        text.remove_prefix(1);
    while (!text.empty() && isTrimmed(text.back()))
        text.remove_suffix(1);
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

// The kinds of file the dialogs offer.
std::vector<FileFilter> meshFilters()
{
    FileFilter filter{"Mesh files", {}};
    for (const std::string& extension : mesh::meshFileExtensions())
        filter.extensions.push_back(extension.substr(1));
    return {filter};
}

const std::vector<FileFilter> kVoxelFilters{{"Voxel files", {"voxels"}}};

// A number of voxels or bricks for people: 56.7 M, 93,970.
std::string formatCount(std::uint64_t count)
{
    if (count >= 10'000'000)
        return std::format("{:.1f} M", static_cast<double>(count) / 1e6);
    std::string digits = std::to_string(count);
    for (std::ptrdiff_t at = static_cast<std::ptrdiff_t>(digits.size()) - 3; at > 0; at -= 3)
        digits.insert(static_cast<std::size_t>(at), ",");
    return digits;
}

// Runs the file dialog; a dialog that cannot open shows as a problem.
std::optional<std::filesystem::path> pickWithDialog(const std::vector<FileFilter>& filters,
                                                    const std::filesystem::path& start, std::string& problem)
{
    try {
        return pickFile(filters, start);
    } catch (const std::exception& error) {
        problem = error.what();
        return std::nullopt;
    }
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
    const ImGuiStyle& style = ImGui::GetStyle();
    rowLabel("Model");
    const float openWidth = ImGui::CalcTextSize("Open...").x + 2.0f * style.FramePadding.x;
    ImGui::SetNextItemWidth(-(openWidth + style.ItemSpacing.x));
    if (ImGui::BeginCombo("##model", modelName(facts_.models.at(wantedModel_)).c_str())) {
        for (std::size_t i = 0; i < facts_.models.size(); ++i) {
            if (ImGui::Selectable(modelName(facts_.models[i]).c_str(), i == wantedModel_) && !locked)
                selectModel(i);
            if (!facts_.models[i].empty()) {
                ImGui::SetItemTooltip("%s\nRight click: show in Explorer, remove", utf8(facts_.models[i]).c_str());
                drawModelMenu(facts_.models[i], true, locked);
            }
        }
        // Files opened in earlier runs that are not in the list: opening one adds it.
        std::vector<std::filesystem::path> recent;
        for (const std::filesystem::path& file : recent_.list("models")) {
            const bool listed = std::any_of(facts_.models.begin(), facts_.models.end(), [&](const auto& model) {
                std::error_code error;
                return !model.empty() && std::filesystem::equivalent(model, file, error);
            });
            if (!listed)
                recent.push_back(file);
        }
        if (!recent.empty()) {
            ImGui::SeparatorText("Recent");
            for (std::size_t i = 0; i < recent.size(); ++i) {
                if (ImGui::Selectable(std::format("{}##recent{}", utf8(recent[i].filename()), i).c_str()) && !locked)
                    requestOpen(recent[i]);
                ImGui::SetItemTooltip("%s\nRight click: show in Explorer, remove", utf8(recent[i]).c_str());
                drawModelMenu(recent[i], false, locked);
            }
        }
        ImGui::EndCombo();
    }
    drawRecycleConfirm();
    ImGui::SameLine();
    ImGui::BeginDisabled(locked);
    if (ImGui::Button("Open...")) {
        if (auto file = pickWithDialog(kVoxelFilters, facts_.importDirectory, problem_))
            requestOpen(*file);
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Opens a voxel file (.voxels) from any folder. It joins the model list.");
    drawModelFacts();
    drawLoadingProgress();
    drawImportMesh();
}

void ControlPanel::drawModelMenu(const std::filesystem::path& file, bool listed, bool locked)
{
    if (!ImGui::BeginPopupContextItem())
        return;
    if (ImGui::MenuItem("Show in Explorer")) {
        try {
            revealFile(file);
        } catch (const std::exception& error) {
            problem_ = error.what();
        }
    }
    // The model on screen (or on its way) stays: the window always shows a model of the list.
    const bool inUse = file == facts_.models.at(facts_.model) || file == facts_.models.at(wantedModel_);
    if (!listed) {
        // A recent file is not loaded: it only leaves the list of recent files.
        if (ImGui::MenuItem("Remove from recent")) {
            try {
                recent_.remove(file);
            } catch (const std::exception& error) {
                problem_ = error.what();
            }
        }
        ImGui::EndPopup();
        return;
    }
    if (ImGui::MenuItem("Remove from list", nullptr, false, !inUse && !locked))
        requestModelListChange({ModelListRequest::Action::Remove, file});
    std::error_code error;
    const bool inModelFolder = std::filesystem::equivalent(file.parent_path(), facts_.importDirectory, error);
    ImGui::SetItemTooltip(inModelFolder ? "The file stays. It is in the model folder, so it is back at the next start."
                                        : "The file stays where it is.");
    if (ImGui::MenuItem("Move to Recycle Bin...", nullptr, false, !inUse && !locked)) {
        recycleCandidate_ = file;
        openRecycleConfirm_ = true;
    }
    if (inUse)
        ImGui::TextDisabled("The model on screen stays in the list.");
    ImGui::EndPopup();
}

void ControlPanel::drawRecycleConfirm()
{
    constexpr const char* kTitle = "Move to Recycle Bin?";
    if (std::exchange(openRecycleConfirm_, false))
        ImGui::OpenPopup(kTitle);
    if (!ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    const std::filesystem::path file = recycleCandidate_.value_or(std::filesystem::path{});
    std::error_code error;
    const std::uintmax_t bytes = std::filesystem::file_size(file, error);
    ImGui::TextUnformatted(utf8(file.filename()).c_str());
    ImGui::TextDisabled("%s, in %s", error ? "size unknown" : formatBytes(bytes).c_str(),
                        utf8(file.parent_path()).c_str());
    ImGui::TextUnformatted("It leaves the model list. You can restore it from the Recycle Bin.");
    ImGui::Spacing();
    if (ImGui::Button("Move to Recycle Bin")) {
        requestModelListChange({ModelListRequest::Action::Recycle, file});
        recycleCandidate_.reset();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        recycleCandidate_.reset();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void ControlPanel::drawModelFacts()
{
    ImGui::TextDisabled("%s", facts_.sceneDetails.c_str());
    if (facts_.voxelCount == 0) {
        ImGui::TextDisabled("%u boxes", facts_.primitiveCount);
        return;
    }
    const double brickVoxels = std::pow(static_cast<double>(facts_.brickSize), 3.0);
    const double filled =
        static_cast<double>(facts_.voxelCount) / (static_cast<double>(facts_.primitiveCount) * brickVoxels);
    ImGui::TextDisabled("%s voxels in %s bricks, %.1f%% filled", formatCount(facts_.voxelCount).c_str(),
                        formatCount(facts_.primitiveCount).c_str(), 100.0 * filled);
    helpMarker(std::format("Filled: the voxels divided by the voxels of all bricks ({} bricks x {}^3). The rest of "
                           "each brick is empty space that the brick test steps through.",
                           facts_.primitiveCount, facts_.brickSize)
                   .c_str());
    const std::optional<voxel::VoxelFileSummary>& summary =
        facts_.model < facts_.modelSummaries.size() ? facts_.modelSummaries[facts_.model] : std::nullopt;
    if (summary) {
        ImGui::TextDisabled("file %s, read in %.0f ms", formatBytes(summary->fileBytes).c_str(),
                            facts_.loadMilliseconds);
        ImGui::SetItemTooltip("%s", utf8(facts_.models.at(facts_.model)).c_str());
    }
}

void ControlPanel::drawLoadingProgress()
{
    if (loading_.empty())
        return;
    const ImGuiStyle& style = ImGui::GetStyle();
    const float cancelWidth = loadingProgress_.cancellable
                                  ? ImGui::CalcTextSize("Cancel").x + 2.0f * style.FramePadding.x + style.ItemSpacing.x
                                  : 0.0f;
    // No share known: a moving bar (a negative fraction in ImGui).
    const float fraction = loadingProgress_.done ? *loadingProgress_.done : -static_cast<float>(ImGui::GetTime());
    const std::string label = loadingProgress_.done ? std::format("{:.0f}%", 100.0f * *loadingProgress_.done) : "";
    ImGui::ProgressBar(fraction, ImVec2(-cancelWidth, 0.0f), label.empty() ? nullptr : label.c_str());
    ImGui::SetItemTooltip("%s", loading_.c_str());
    if (loadingProgress_.cancellable) {
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            cancelRequest_ = true;
        ImGui::SetItemTooltip("Stops the import. No voxel file is written.");
    }
}

void ControlPanel::drawImportMesh()
{
    if (!ImGui::TreeNodeEx("Import mesh", ImGuiTreeNodeFlags_SpanAvailWidth))
        return;
    const bool locked = measuring() || !loading_.empty();
    const ImGuiStyle& style = ImGui::GetStyle();
    const auto setPath = [this](const std::filesystem::path& path) {
        const std::string text = utf8(path);
        if (text.size() >= importPath_.size()) {
            problem_ = "The path is too long for the field: " + text;
            return;
        }
        std::copy(text.begin(), text.end(), importPath_.begin());
        importPath_[text.size()] = '\0';
    };

    // The path field, a list of recent meshes and the file dialog, on one line.
    rowLabel("Mesh file");
    const float browseWidth = ImGui::CalcTextSize("Browse...").x + 2.0f * style.FramePadding.x;
    ImGui::SetNextItemWidth(-(ImGui::GetFrameHeight() + browseWidth + style.ItemSpacing.x));
    ImGui::InputTextWithHint("##import path", ".ply, .obj, .gltf or .glb", importPath_.data(), importPath_.size());
    ImGui::SameLine(0.0f, 0.0f);
    const std::vector<std::filesystem::path> recentMeshes = recent_.list("meshes");
    ImGui::BeginDisabled(recentMeshes.empty());
    if (ImGui::BeginCombo("##recent meshes", nullptr, ImGuiComboFlags_NoPreview | ImGuiComboFlags_PopupAlignLeft)) {
        for (std::size_t i = 0; i < recentMeshes.size(); ++i) {
            if (ImGui::Selectable(std::format("{}##mesh{}", utf8(recentMeshes[i].filename()), i).c_str()))
                setPath(recentMeshes[i]);
            ImGui::SetItemTooltip("%s", utf8(recentMeshes[i]).c_str());
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Recent mesh files");
    ImGui::SameLine();
    const std::filesystem::path mesh = pathFromField(importPath_.data());
    ImGui::BeginDisabled(locked);
    if (ImGui::Button("Browse...")) {
        // Start where the mesh of the field or the last mesh is.
        std::filesystem::path start = mesh.parent_path();
        if (start.empty() && !recentMeshes.empty())
            start = recentMeshes.front().parent_path();
        if (auto file = pickWithDialog(meshFilters(), start, problem_))
            setPath(*file);
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Picks a mesh file: PLY, OBJ, glTF (.gltf, .glb).");

    drawImportResolution();
    voxel::VoxelizeOptions& voxelize = importChoice_.voxelize;

    rowLabel("Up axis");
    for (const voxel::UpAxis axis : {voxel::UpAxis::Y, voxel::UpAxis::Z}) {
        if (axis != voxel::UpAxis::Y)
            ImGui::SameLine();
        const std::string label = std::format("+{}##import up", voxel::upAxisName(axis));
        if (ImGui::RadioButton(label.c_str(), importChoice_.upAxis == axis))
            importChoice_.upAxis = axis;
    }
    helpMarker("The axis of the mesh that points up. The renderer uses +y. Scans such as Lucy use +z: they are "
               "turned a quarter turn around x.");

    rowLabel("Rule");
    for (const voxel::VoxelizationRule rule :
         {voxel::VoxelizationRule::Sampled, voxel::VoxelizationRule::Conservative}) {
        if (rule != voxel::VoxelizationRule::Sampled)
            ImGui::SameLine();
        const std::string label = std::format("{}##import rule", voxel::ruleName(rule));
        if (ImGui::RadioButton(label.c_str(), voxelize.rule == rule))
            voxelize.rule = rule;
    }
    helpMarker("sampled: Reichl et al. 2012. A line through the centre of every grid column, along each axis; a voxel "
               "is filled where a line crosses a triangle. Thin, and no ray passes through the surface.\n"
               "conservative: every voxel that a triangle touches is filled. Thicker.");

    // The file the import would read and the voxel file it would write.
    std::error_code error;
    const char* issue = nullptr;
    if (voxelize.resolution % kResolutionStep != 0)
        issue = "Resolution: not a multiple of 32";
    else if (!mesh.empty() && !std::filesystem::is_regular_file(mesh, error))
        issue = "No such file";
    else if (!mesh.empty() && !mesh::isMeshFile(mesh))
        issue = "Not a mesh file (.ply, .obj, .gltf, .glb)";
    if (!mesh.empty() && issue == nullptr)
        drawImportEstimate(mesh);
    ImGui::BeginDisabled(locked || mesh.empty() || issue != nullptr);
    if (ImGui::Button("Import")) {
        importChoice_.mesh = mesh;
        requestImport(importChoice_);
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Voxelizes the mesh on a worker thread, writes the voxel file to the model folder and "
                          "opens it. Cancel stops it. Same as: thesis_model voxelize.");
    if (issue) {
        ImGui::SameLine();
        ImGui::TextColored(kProblemColor, "%s", issue);
    } else if (!mesh.empty()) {
        voxel::MeshImportOptions options{importChoice_.voxelize, importChoice_.upAxis, {}, facts_.importDirectory};
        const std::filesystem::path target = voxel::importTarget(mesh, options);
        ImGui::SameLine();
        ImGui::TextDisabled("to %s", utf8(target.filename()).c_str());
        if (std::filesystem::exists(target, error)) {
            ImGui::SameLine();
            ImGui::TextColored(kHighlightColor, "(replaces)");
            ImGui::SetItemTooltip("A voxel file of this name is in the model folder. The import writes over it.");
        }
    }
    ImGui::TreePop();
}

void ControlPanel::drawImportResolution()
{
    voxel::VoxelizeOptions& voxelize = importChoice_.voxelize;
    rowLabel("Resolution");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5.5f);
    const std::string preview = customResolution_ ? std::string("custom") : std::to_string(voxelize.resolution);
    if (ImGui::BeginCombo("##import resolution", preview.c_str())) {
        for (const std::uint32_t resolution : kImportResolutions) {
            if (ImGui::Selectable(std::to_string(resolution).c_str(),
                                  !customResolution_ && voxelize.resolution == resolution)) {
                voxelize.resolution = resolution;
                customResolution_ = false;
            }
        }
        if (ImGui::Selectable("custom", customResolution_))
            customResolution_ = true;
        ImGui::EndCombo();
    }
    if (customResolution_) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
        int value = static_cast<int>(voxelize.resolution);
        if (ImGui::InputInt("##custom resolution", &value, static_cast<int>(kResolutionStep), 256))
            voxelize.resolution = static_cast<std::uint32_t>(
                std::clamp(value, static_cast<int>(kResolutionStep), static_cast<int>(kMaxResolution)));
    }
    ImGui::SameLine();
    ImGui::TextDisabled("voxels");
    helpMarker(std::format("Voxels along the longest side of the mesh. The voxels lie on the surface, so their number "
                           "and the time grow with the square of the resolution. The models of the thesis use 2048.\n"
                           "custom: any multiple of {} up to {}, so that every brick size divides the grid.",
                           kResolutionStep, kMaxResolution)
                   .c_str());
}

void ControlPanel::drawImportEstimate(const std::filesystem::path& mesh)
{
    // An earlier import of the same mesh with the same rule: the one nearest in resolution.
    const voxel::VoxelizeOptions& voxelize = importChoice_.voxelize;
    const std::string meshName = utf8(mesh.stem());
    const std::string_view rule = voxel::ruleName(voxelize.rule);
    std::size_t from = facts_.models.size();
    double nearest = 0.0;
    for (std::size_t i = 0; i < facts_.modelSummaries.size(); ++i) {
        const std::optional<voxel::VoxelFileSummary>& summary = facts_.modelSummaries[i];
        if (!summary || summary->source.mesh != meshName || summary->source.rule != rule ||
            summary->source.resolution == 0)
            continue;
        const double distance = std::abs(
            std::log(static_cast<double>(voxelize.resolution) / static_cast<double>(summary->source.resolution)));
        if (from == facts_.models.size() || distance < nearest) {
            from = i;
            nearest = distance;
        }
    }
    if (from == facts_.models.size()) {
        ImGui::TextDisabled("Size: no earlier import of %s to scale from", meshName.c_str());
        helpMarker("After one import of this mesh (same rule), the panel estimates the size at other resolutions.");
        return;
    }

    // The voxels lie on the surface: they grow with the square of the resolution.
    const voxel::VoxelFileSummary& summary = *facts_.modelSummaries[from];
    const double scale = std::pow(static_cast<double>(voxelize.resolution) / summary.source.resolution, 2.0);
    const auto voxels = static_cast<std::uint64_t>(static_cast<double>(summary.voxels) * scale);
    const auto fileBytes = static_cast<std::uint64_t>(static_cast<double>(summary.fileBytes) * scale);
    std::string text = std::format("about {} voxels, file {}", formatCount(voxels), formatBytes(fileBytes));
    std::string help = std::format("Scaled from {} (resolution {}): the voxels lie on the surface, so they grow with "
                                   "the square of the resolution.",
                                   utf8(facts_.models.at(from).filename()), summary.source.resolution);
    if (facts_.voxelCount > 0 && !facts_.encodings.empty()) {
        const double bitsPerVoxel = 8.0 * static_cast<double>(facts_.sceneBytes) / facts_.voxelCount;
        const auto gpuBytes = static_cast<std::uint64_t>(static_cast<double>(voxels) * bitsPerVoxel / 8.0);
        text += std::format(", GPU {}", formatBytes(gpuBytes));
        help += std::format("\nGPU: at {:.1f} bits per voxel, the encoding on screen ({}). Another model packs a "
                            "little differently into bricks.",
                            bitsPerVoxel, facts_.encodingChoices.describe(facts_.encoding));
    }
    ImGui::TextDisabled("%s", text.c_str());
    helpMarker(help.c_str());
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
