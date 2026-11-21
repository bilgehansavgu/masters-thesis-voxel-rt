// The Inspector window: the picked pixel, the walk of its ray in stages, the search structure of
// the tracer, and every step of the ray log.

#include "ControlPanel.h"

#include "PanelWidgets.h"

#include <imgui.h>

#include <bit>
#include <format>

namespace thesis::app {

void ControlPanel::drawInspector()
{
    const PanelFacts::Tracer& tracer = facts_.tracers.at(activeTracer_);
    ImGui::Text("Tracer: %s", tracerLabel(tracer.name));
    if (!tracer.method.empty())
        helpMarker(tracer.method.c_str());
    if (!tracer.findsHits) {
        ImGui::TextDisabled("Baseline: writes a miss for every ray.");
        return;
    }

    ImGui::SeparatorText("Search structure");
    structure_.controls(activeTracer_, tracer.diagram, facts_.geometry, facts_.sceneBounds);

    ImGui::SeparatorText("Ray");
    drawRayWalk();
}

void ControlPanel::drawRayWalk()
{
    if (!inspectedPixel_) {
        if (ImGui::Button(picking_ ? "Cancel" : "Pick a pixel"))
            picking_ = !picking_;
        helpMarker("Right click on the image picks the pixel under the mouse. Its ray is recorded on the GPU, step "
                   "by step: the search, the brick, the columns, the voxel it hits.");
        return;
    }
    ImGui::Text("Pixel %u, %u", inspectedPixel_->first, inspectedPixel_->second);
    ImGui::SameLine();
    if (ImGui::SmallButton("Pick another")) {
        stopInspecting();
        picking_ = true;
        return;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Stop (Esc)")) {
        stopInspecting();
        return;
    }
    if (!lesson_) {
        ImGui::TextDisabled("Recording the ray on the GPU ...");
        return;
    }

    // Stage bar: one button per stage, with the number of events. Hover: what the stage means.
    for (std::size_t s = 0; s < lesson_->stages.size(); ++s) {
        const RayStage& stage = lesson_->stages[s];
        if (s > 0)
            ImGui::SameLine(0.0f, 4.0f);
        const std::string label = stage.events.size() > 1
                                      ? std::format("{} ({})##stage{}", stage.name, stage.events.size(), s)
                                      : std::format("{}##stage{}", stage.name, s);
        const bool current = s == lessonStage_;
        if (current)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::SmallButton(label.c_str())) {
            lessonStage_ = s;
            lessonEvent_ = 0;
            orientCloseUp();
        }
        if (current)
            ImGui::PopStyleColor();
        ImGui::SetItemTooltip("%s", stage.tooltip.c_str());
    }
    ImGui::TextDisabled("%s", lesson_->stages.at(lessonStage_).summary.c_str());

    // Event stepper: one recorded event at a time (arrow keys step across stages).
    const bool atStart = lessonStage_ == 0 && lessonEvent_ == 0;
    const bool atEnd = lessonStage_ + 1 == lesson_->stages.size() &&
                       lessonEvent_ + 1 >= lesson_->stages.at(lessonStage_).events.size();
    ImGui::BeginDisabled(atStart);
    if (ImGui::ArrowButton("previous event", ImGuiDir_Left))
        previousRayStep();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(atEnd);
    if (ImGui::ArrowButton("next event", ImGuiDir_Right))
        nextRayStep();
    ImGui::EndDisabled();
    ImGui::SameLine();
    // The arrows can change the stage: read it only now, so that the event index belongs to it.
    const RayStage& stage = lesson_->stages.at(lessonStage_);
    if (stage.events.size() > 1) {
        int position = static_cast<int>(lessonEvent_) + 1;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
        if (ImGui::SliderInt("##event", &position, 1, static_cast<int>(stage.events.size()), "event %d"))
            lessonEvent_ = static_cast<std::size_t>(position - 1);
        ImGui::SameLine();
        ImGui::TextDisabled("/ %zu", stage.events.size());
        ImGui::SameLine();
    }
    if (ImGui::SmallButton(observing_ ? "Camera view (V)" : "Side view (V)"))
        toggleSideView();

    const RayEvent& event = stage.events.at(lessonEvent_);
    if (event.positive)
        ImGui::TextColored(kHighlightColor, "%s", event.label.c_str());
    else
        ImGui::TextUnformatted(event.label.c_str());

    if (stage.closeUp && lesson_->brickCell) {
        if (ImGui::SmallButton("Go to brick"))
            showBrick();
        ImGui::SetItemTooltip("Camera close to this brick, from the capture direction. V returns.");
        drawBrickCloseUp(*lesson_, lessonStage_, lessonEvent_, closeUp_);
        ImGui::TextDisabled("grey full voxel | cyan brick cell | gold ray, column, hit");
        if (stage.cumulative)
            ImGui::TextDisabled("blue columns before | orange mipmap square");
    }

    if (comparison_ && rayLog_ && !rayLog_->lostSteps && ImGui::BeginTable("same pixel", 3, ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Same pixel", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Structure steps", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Bricks tested", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();
        const auto row = [&](std::size_t index, const core::RayWork& work) {
            const PanelFacts::Tracer& tracer = facts_.tracers[index];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(tracerLabel(tracer.name));
            ImGui::TableNextColumn();
            if (tracer.countsStructureSteps)
                ImGui::Text("%u %s", work.structureSteps, tracer.structureStep.c_str());
            else
                ImGui::TextDisabled("not reported");
            ImGui::TableNextColumn();
            ImGui::Text("%u", work.primitiveTests);
        };
        row(comparison_->first, comparison_->second);
        row(activeTracer_, rayLogWork_);
        ImGui::EndTable();
    }
    drawRayLog();
}

void ControlPanel::drawRayLog()
{
    if (!sectionHeader("Ray log", false)) {
        return;
    }
    const PanelFacts::Tracer& tracer = facts_.tracers.at(activeTracer_);
    if (!tracer.countsWork) {
        ImGui::TextDisabled("This tracer does not count work: no log.");
        return;
    }
    if (!rayLog_) {
        ImGui::TextDisabled("The log comes with the next frame.");
        return;
    }

    const core::RayWork& work = rayLogWork_;
    if (ImGui::BeginTable("ray work", 2)) {
        const auto row = [](const char* name, const std::string& value) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", name);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(value.c_str());
        };
        row("Structure steps", tracer.countsStructureSteps
                                   ? std::format("{} {}", work.structureSteps, tracer.structureStep)
                                   : std::string("not reported by this tracer"));
        row("Bricks tested", std::to_string(work.primitiveTests));
        row("Cells, runs", std::format("{}, {}", work.cells, work.runs));
        if (work.mipmapCells > 0) {
            const std::uint32_t largest =
                work.skippedLevels == 0 ? 0u : static_cast<std::uint32_t>(std::bit_width(work.skippedLevels) - 1);
            row("Mipmap cells read", std::to_string(work.mipmapCells));
            row("Squares skipped",
                largest > 0 ? std::format("{}, largest {}x{} cells", work.mipmapSkips, 1u << largest, 1u << largest)
                            : std::string("0"));
        }
        ImGui::EndTable();
    }
    if (rayLog_->lostSteps)
        ImGui::TextColored(kProblemColor, "Log truncated: the counts cover the recorded steps only.");

    ImGui::Checkbox("Only hits and skipped squares", &onlyImportantSteps_);
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy as text")) {
        ImGui::SetClipboardText(rayLogAsText(rayLogLines_).c_str());
    }
    helpMarker("t: distance along the ray where the step begins. Cells (u, v) are cells of the primary grid of the "
               "brick; heights count voxels along its height axis.");

    std::vector<std::size_t> shown;
    shown.reserve(rayLogLines_.size());
    for (std::size_t i = 0; i < rayLogLines_.size(); ++i) {
        if (!onlyImportantSteps_ || rayLogLines_[i].important) {
            shown.push_back(i);
        }
    }
    if (ImGui::BeginTable("steps", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV,
                          {0.0f, 18.0f * ImGui::GetTextLineHeightWithSpacing()})) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("t", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 4.5f);
        ImGui::TableSetupColumn("Step", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(shown.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const RayLogLine& line = rayLogLines_[shown[static_cast<std::size_t>(row)]];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (line.hasT) {
                    ImGui::TextDisabled("%.4f", line.t);
                }
                ImGui::TableNextColumn();
                const float indent = 14.0f * static_cast<float>(line.depth) + 1.0f;
                ImGui::Indent(indent);
                if (line.important) {
                    ImGui::TextColored(kHighlightColor, "%s", line.text.c_str());
                } else {
                    ImGui::TextUnformatted(line.text.c_str());
                }
                ImGui::Unindent(indent);
            }
        }
        ImGui::EndTable();
    }
}

} // namespace thesis::app
