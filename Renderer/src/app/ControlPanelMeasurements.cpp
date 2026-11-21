// The Measurements window, under the image: live timing of the tracers, the work of the rays,
// the comparison at a fixed view, the memory of the scene, and the command of the frame. The
// times here are live numbers of the window; thesis numbers come from --benchmark.

#include "ControlPanel.h"

#include "PanelWidgets.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <format>

namespace thesis::app {

void ControlPanel::drawMeasurements()
{
    if (!ImGui::BeginTabBar("measurements"))
        return;
    if (ImGui::BeginTabItem("Timing", nullptr, measuring() ? ImGuiTabItemFlags_SetSelected : 0)) {
        drawTiming();
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Work per ray")) {
        drawWorkSummary();
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Compare view")) {
        drawComparison();
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Memory")) {
        drawMemory();
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Command")) {
        drawCommand();
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}

void ControlPanel::drawTiming()
{
    // The quick timing table on the left, the stages of the frame on screen on the right.
    if (!ImGui::BeginTable("timing layout", 2, ImGuiTableFlags_SizingStretchProp))
        return;
    ImGui::TableSetupColumn("tracers", ImGuiTableColumnFlags_WidthStretch, 3.0f);
    ImGui::TableSetupColumn("frame", ImGuiTableColumnFlags_WidthStretch, 1.4f);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();

    const std::optional<double> floor = floorMilliseconds();
    const double rays = static_cast<double>(facts_.width) * facts_.height;
    bool anyFromNewShader = false;
    if (ImGui::BeginTable("tracers", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Tracer", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Trace ms", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Mrays/s", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Above baseline ms", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Build", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < facts_.tracers.size(); ++i) {
            const PanelFacts::Tracer& tracer = facts_.tracers[i];
            const TraceTime& time = traceTimes_[i];
            const bool current = time.cameraVersion == cameraVersion_;
            const bool valid = time.cameraVersion != 0;
            const bool trusted = valid && current && !time.fromNewShader;
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            if (ImGui::Selectable(tracerLabel(tracer.name), i == activeTracer_, ImGuiSelectableFlags_SpanAllColumns) &&
                !measuring())
                selectTracer(i);

            ImGui::TableNextColumn();
            if (!valid)
                ImGui::TextDisabled("-");
            else if (trusted)
                ImGui::Text("%.3f", time.milliseconds);
            else
                ImGui::TextDisabled("%.3f%s", time.milliseconds, time.fromNewShader ? " *" : " (old view)");
            anyFromNewShader = anyFromNewShader || (valid && time.fromNewShader);

            ImGui::TableNextColumn();
            if (valid && time.milliseconds > 0.0)
                (trusted ? ImGui::Text : ImGui::TextDisabled)("%.0f", rays / (time.milliseconds * 1e3));
            else
                ImGui::TextDisabled("-");

            ImGui::TableNextColumn();
            if (tracer.findsHits && floor && valid && !time.fromNewShader)
                (trusted ? ImGui::Text : ImGui::TextDisabled)("%.3f", time.milliseconds - *floor);
            else
                ImGui::TextDisabled("-");

            ImGui::TableNextColumn();
            if (tracer.buildMilliseconds > 0.0)
                ImGui::TextDisabled("%.1f ms %s", tracer.buildMilliseconds, tracer.buildOnCpu ? "CPU" : "GPU");
            else
                ImGui::TextDisabled("-");
        }
        ImGui::EndTable();
    }

    if (measuring()) {
        const std::size_t tracer = measurement_->tracer;
        ImGui::Text("Measuring %s (%zu of %zu)", tracerLabel(facts_.tracers.at(tracer).name), tracer + 1,
                    facts_.tracers.size());
        ImGui::SameLine();
        if (ImGui::SmallButton("Stop")) {
            stopMeasuring();
        } else if (shaderIsNew(tracer)) {
            ImGui::SameLine();
            ImGui::TextDisabled("new shader: %.0f s warm-up left", kNewShaderSeconds - shaderSeconds(tracer));
        }
    } else if (!windowFocused_) {
        ImGui::TextDisabled("Window in the background: timing paused.");
    } else if (ImGui::Button("Quick timing (M)")) {
        measureAll();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("window numbers, not thesis numbers");
    helpMarker("Quick timing runs each tracer for 60 frames at this camera, after 10 frames and a warm-up of a new "
               "shader, and shows the median. * : the shader ran less than 7 s, so the time is too high. Old view: "
               "the camera moved since. Above baseline: minus the time of the baseline tracer, a hint only. Thesis "
               "numbers come from --benchmark.");
    if (anyFromNewShader)
        ImGui::TextDisabled("* new shader: time too high");

    ImGui::TableNextColumn();
    ImGui::TextDisabled("Frame on screen");
    if (ImGui::BeginTable("stages", 2)) {
        const auto row = [](const char* stage, double milliseconds) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(stage);
            ImGui::TableNextColumn();
            ImGui::Text("%8.3f ms", milliseconds);
        };
        row("Ray generation", rayGenerationMs_.value);
        row("Trace", traceMs_.value);
        row("Shading", shadingMs_.value);
        ImGui::EndTable();
    }
    ImGui::TextDisabled("%s", lastFrameCounted_ ? "trace: counting shader" : "trace: timed shader");
    ImGui::EndTable();
}

void ControlPanel::drawWorkSummary()
{
    if (!resolve::showsWork(shading_.mode)) {
        ImGui::TextDisabled("Choose a work view (6 to =) in Output.");
        return;
    }
    if (!workSummary_ || workSummary_->rays() == 0) {
        ImGui::TextDisabled("Comes with the next frames.");
        return;
    }
    const RayWorkSummary& summary = *workSummary_;
    ImGui::Text("%.1f %% of the rays hit",
                100.0 * static_cast<double>(summary.raysWithHit()) / static_cast<double>(summary.rays()));
    helpMarker("Averages over the frame on screen, about once a second. Thesis numbers come from --benchmark "
               "(summary.ray_work).");
    const RayWorkSummary::Means all = summary.meanPerRay();
    const RayWorkSummary::Means hit = summary.meanPerRayWithHit();
    const core::RayWork& most = summary.most();
    if (ImGui::BeginTable("work", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Per ray", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("All rays", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Rays with hit", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Most", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();
        const auto row = [](const char* name, double perRay, double perHit, std::uint32_t largest) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(name);
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", perRay);
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", perHit);
            ImGui::TableNextColumn();
            ImGui::Text("%u", largest);
        };
        if (facts_.tracers.at(activeTracer_).countsStructureSteps)
            row("Search structure steps", all.structureSteps, hit.structureSteps, most.structureSteps);
        row("Primitive tests", all.primitiveTests, hit.primitiveTests, most.primitiveTests);
        row("Mipmap cells read", all.mipmapCells, hit.mipmapCells, most.mipmapCells);
        row("  skipped squares", all.mipmapSkips, hit.mipmapSkips, most.mipmapSkips);
        row("Cells visited", all.cells, hit.cells, most.cells);
        row("Runs tested", all.runs, hit.runs, most.runs);
        ImGui::EndTable();
    }
    if (most.skippedLevels != 0) {
        std::string text = "Largest square skipped:";
        const auto& byLevel = summary.raysByLargestSkip();
        for (std::size_t level = 0; level < byLevel.size(); ++level) {
            if (byLevel[level] == 0) {
                continue;
            }
            const double percent = 100.0 * static_cast<double>(byLevel[level]) / static_cast<double>(summary.rays());
            text += level == 0 ? std::format(" none {:.1f} %", percent)
                               : std::format(", {0}x{0} {1:.1f} %", 1u << level, percent);
        }
        wrapped(text, true);
    }
}

void ControlPanel::drawComparison()
{
    std::vector<std::string> choices;
    const bool surface = facts_.tracers.at(activeTracer_).findsHits;
    if (facts_.voxelCount && surface)
        choices.emplace_back();
    for (const auto& tracer : facts_.tracers)
        if (tracer.findsHits && tracer.name != tracerName() && surface)
            choices.push_back(tracer.name);
    if (choices.empty()) {
        ImGui::TextDisabled("Choose a tracer that finds hits.");
    } else {
        compareChoice_ = std::clamp(compareChoice_, 0, static_cast<int>(choices.size()) - 1);
        const auto label = [this](const std::string& name) -> std::string {
            return name.empty() ? std::string("Mipmap off / on") : std::string(tracerLabel(name));
        };
        rowLabel("Compare");
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
        if (ImGui::BeginCombo("##compare", label(choices[compareChoice_]).c_str())) {
            for (int i = 0; i < static_cast<int>(choices.size()); ++i)
                if (ImGui::Selectable(label(choices[i]).c_str(), i == compareChoice_))
                    compareChoice_ = i;
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("with %s", tracerLabel(tracerName()));
        ImGui::SameLine();
        ImGui::BeginDisabled(measuring() || compareRequest_.has_value());
        if (ImGui::Button("Run"))
            compareRequest_ = choices[compareChoice_];
        ImGui::EndDisabled();
        helpMarker("The benchmark protocol on the exact camera rays of this view: warm-up, repeats, median. Both "
                   "runs see the same rays; the hits are compared with the rule of the agreement test. Results "
                   "files are saved. The window pauses while it runs.");
        if (compareRequest_)
            ImGui::TextDisabled("Measuring ...");
    }
    if (!result_)
        return;
    const auto& result = *result_;
    const core::HitComparison& same = result.hits;
    if (same.agrees())
        ImGui::Text("Same hits: %zu of %zu rays (relative distance within %.0e)", same.rays - same.disagreements(),
                    same.rays, static_cast<double>(core::HitComparison::kRelativeTolerance));
    else
        ImGui::TextColored(kProblemColor, "Different hits: %zu of %zu rays", same.disagreements(), same.rays);
    if (same.primitiveMismatch + same.attributeMismatch > 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%zu touching voxels at equal distance)", same.primitiveMismatch + same.attributeMismatch);
    }
    const auto& first = result.rows.front();
    const auto& second = result.rows.back();
    const bool skippingPair = first.encoding != second.encoding;
    if (ImGui::BeginTable("comparison", 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Tracer", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Mipmap", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Trace ms", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Spread %", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Cells/ray", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Structure", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();
        for (const auto& row : result.rows) {
            ImGui::PushID(row.file.string().c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(tracerLabel(row.tracer));
            if (!row.settled)
                ImGui::SetItemTooltip("Warm-up ended before the GPU clock and the time were steady.");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(encodingSkips(row.encoding) ? "on" : "off");
            ImGui::TableNextColumn();
            if (row.settled)
                ImGui::Text("%.3f", row.medianMs);
            else
                ImGui::TextColored(kProblemColor, "%.3f !", row.medianMs);
            ImGui::TableNextColumn();
            ImGui::Text("%.1f", row.spreadPercent);
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", row.cellsPerRay);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(formatBytes(row.structureBytes).c_str());
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Show")) {
                stopInspecting();
                camera_ = result.camera;
                cameraMoved();
                for (std::size_t i = 0; i < facts_.tracers.size(); ++i)
                    if (facts_.tracers[i].name == row.tracer)
                        selectTracer(i);
                for (std::size_t i = 0; i < facts_.encodings.size(); ++i)
                    if (facts_.encodings[i].name == row.encoding)
                        wantedEncoding_ = i;
                wantedBrickSize_ = resultBrickSize_;
                selectView(resolve::ShadingMode::Shaded);
                picking_ = true;
                pickAfterReload_ = wantedEncoding_ != facts_.encoding || wantedBrickSize_ != facts_.brickSize;
            }
            ImGui::SetItemTooltip("Camera, tracer and encoding of this row");
            ImGui::SameLine();
            if (ImGui::SmallButton("Path"))
                ImGui::SetClipboardText(std::filesystem::absolute(row.file).string().c_str());
            ImGui::SetItemTooltip("Copy the path of the results file");
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (skippingPair && first.medianMs > 0.0)
        ImGui::TextDisabled("Mipmap on: %+.2f cells per ray, trace time %+.1f %%",
                            second.cellsPerRay - first.cellsPerRay, 100.0 * (second.medianMs / first.medianMs - 1.0));
}

void ControlPanel::drawMemory()
{
    if (!ImGui::BeginTable("memory layout", 2, ImGuiTableFlags_SizingStretchProp))
        return;
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    if (ImGui::BeginTable("buffers", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Buffer on the GPU", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();
        const auto row = [](const std::string& name, std::uint64_t bytes) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(formatBytes(bytes).c_str());
        };
        for (const PanelFacts::SceneBuffer& buffer : facts_.sceneBuffers)
            row(buffer.name, buffer.bytes);
        row("scene total", facts_.sceneBytes);
        row("rays + hits", facts_.rayAndHitBytes);
        ImGui::EndTable();
    }

    ImGui::TableNextColumn();
    if (!facts_.encodings.empty() &&
        ImGui::BeginTable("encodings", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn(std::format("Encoding, bricks of {}", facts_.brickSize).c_str(),
                                ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Bits/voxel", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < facts_.encodings.size(); ++i) {
            const std::optional<EncodingSize>& size = encodingSizes_[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(facts_.encodingChoices.describe(i).c_str());
            ImGui::SetItemTooltip("%s", facts_.encodings[i].name.c_str());
            ImGui::TableNextColumn();
            if (size)
                ImGui::TextUnformatted(formatBytes(size->bytes).c_str());
            else
                ImGui::TextDisabled("-");
            ImGui::TableNextColumn();
            if (size)
                ImGui::Text("%.2f", size->bitsPerVoxel);
            else
                ImGui::TextDisabled("-");
        }
        ImGui::EndTable();
        ImGui::TextDisabled("-: not shown yet at this brick size");
    }
    ImGui::EndTable();
}

void ControlPanel::drawCommand()
{
    if (ImGui::Button("Copy screenshot command"))
        copyCommand(CommandForm::Screenshot);
    ImGui::SameLine();
    if (ImGui::Button("Copy benchmark command"))
        copyCommand(CommandForm::Benchmark);
    helpMarker("Screenshot: writes the frame on screen to frame.png, pixel for pixel. Benchmark: measures this scene "
               "with the protocol of the thesis, as an orbit at a camera preset, else at this fixed view.");
    if (facts_.command) {
        const WindowState state = windowState();
        for (const auto& [name, form] :
             {std::pair{"screenshot", CommandForm::Screenshot}, std::pair{"benchmark", CommandForm::Benchmark}}) {
            ImGui::TextDisabled("%s", name);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(facts_.command(state, form).c_str());
            ImGui::PopTextWrapPos();
        }
    }
}

} // namespace thesis::app
