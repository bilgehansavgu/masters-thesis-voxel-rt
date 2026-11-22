// The Measurements window, under the image: live timing of the tracers, the work of the rays,
// the comparison at a fixed view, the memory of the scene, and the command of the frame. The
// times here are live numbers of the window; thesis numbers come from --benchmark.

#include "ControlPanel.h"

#include "PanelWidgets.h"
#include "PathText.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
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
        ImGui::TextDisabled("Choose a work view (6 to =, C) in Output.");
        return;
    }
    if (!workSummary_ || workSummary_->rays() == 0) {
        ImGui::TextDisabled("Comes with the next frames.");
        return;
    }
    const RayWorkSummary& summary = *workSummary_;
    if (!ImGui::BeginTable("work layout", 2, ImGuiTableFlags_SizingStretchProp))
        return;
    ImGui::TableSetupColumn("table", ImGuiTableColumnFlags_WidthStretch, 3.0f);
    ImGui::TableSetupColumn("histogram", ImGuiTableColumnFlags_WidthStretch, 2.0f);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Text("%.1f %% of the rays hit",
                100.0 * static_cast<double>(summary.raysWithHit()) / static_cast<double>(summary.rays()));
    helpMarker("Over the frame on screen, about once a second. Thesis numbers come from --benchmark "
               "(summary.ray_work). p99: 99 % of the rays with work stay at or below it. Warp use: the work of all "
               "rays divided by the work their warps were busy for (warp size x the largest count of each warp); "
               "100 % means no ray waited for another. Click a row for its histogram; the arrow opens the inspector "
               "on the ray with the most.");
    const RayWorkSummary::Means all = summary.meanPerRay();
    const RayWorkSummary::Means hit = summary.meanPerRayWithHit();
    const core::RayWork& most = summary.most();
    const bool warps = summary.warpSize() > 0;
    // The histogram of the chosen counter, or of the primitive tests (every tracer that counts
    // work counts them) while the active tracer does not report the chosen one. The choice stays
    // for the next tracer that reports it.
    const WorkCounter shown = activeTracerCounts(histogramCounter_) ? histogramCounter_ : WorkCounter::PrimitiveTests;
    if (ImGui::BeginTable("work", warps ? 7 : 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Per ray", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("All rays", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("With hit", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("p99", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Most", ImGuiTableColumnFlags_WidthFixed);
        if (warps)
            ImGui::TableSetupColumn("Warp use", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();
        const auto row = [&](const char* name, WorkCounter counter, double perRay, double perHit,
                             std::uint32_t largest) {
            ImGui::PushID(static_cast<int>(counter));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Selectable(name, shown == counter,
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
                histogramCounter_ = counter;
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", perRay);
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", perHit);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(resolve::formatCount(summary.percentile(counter, 0.99)).c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%u", largest);
            if (warps) {
                ImGui::TableNextColumn();
                if (const std::optional<double> use = summary.warpUse(counter))
                    ImGui::Text("%.0f %%", 100.0 * *use);
                else
                    ImGui::TextDisabled("-");
            }
            ImGui::TableNextColumn();
            const bool current = costliestIsCurrent();
            ImGui::BeginDisabled(largest == 0 || !current);
            if (ImGui::ArrowButton("costliest", ImGuiDir_Right))
                inspectCostliest(counter);
            ImGui::EndDisabled();
            if (current)
                ImGui::SetItemTooltip("Inspect the ray with the most (%u)", largest);
            else
                ImGui::SetItemTooltip("The camera moved since these counts; the next summary comes within a second.");
            ImGui::PopID();
        };
        if (activeTracerCounts(WorkCounter::StructureSteps))
            row("Search structure steps", WorkCounter::StructureSteps, all.structureSteps, hit.structureSteps,
                most.structureSteps);
        row("Primitive tests", WorkCounter::PrimitiveTests, all.primitiveTests, hit.primitiveTests,
            most.primitiveTests);
        row("Mipmap cells read", WorkCounter::MipmapCells, all.mipmapCells, hit.mipmapCells, most.mipmapCells);
        row("  skipped squares", WorkCounter::MipmapSkips, all.mipmapSkips, hit.mipmapSkips, most.mipmapSkips);
        row("Cells visited", WorkCounter::Cells, all.cells, hit.cells, most.cells);
        row("Runs tested", WorkCounter::Runs, all.runs, hit.runs, most.runs);
        if (activeTracerCounts(WorkCounter::Cycles))
            row("GPU clock cycles", WorkCounter::Cycles, all.cycles, hit.cycles, most.cycles);
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
    ImGui::TableNextColumn();
    drawHistogram(shown);
    ImGui::EndTable();
}

bool ControlPanel::activeTracerCounts(WorkCounter counter) const
{
    const PanelFacts::Tracer& tracer = facts_.tracers.at(activeTracer_);
    if (counter == WorkCounter::StructureSteps)
        return tracer.countsStructureSteps;
    if (counter == WorkCounter::Cycles)
        return tracer.countsCycles;
    return tracer.countsWork;
}

void ControlPanel::drawHistogram(WorkCounter counter)
{
    // Rays by the count of `counter`, one bar per power of two (0, 1, 2-3, 4-7, ...), rays with a
    // hit below and rays without a hit on top. The height is log10(1 + rays): the few costly rays stay
    // visible next to the many cheap ones.
    const RayWorkSummary& summary = *workSummary_;
    const auto misses = summary.octaves(counter, false);
    const auto hits = summary.octaves(counter, true);
    std::size_t last = 0;
    std::uint64_t tallest = 1;
    for (std::size_t k = 0; k < misses.size(); ++k) {
        if (misses[k] + hits[k] > 0)
            last = k;
        tallest = std::max(tallest, misses[k] + hits[k]);
    }
    static constexpr const char* kNames[] = {"Search structure steps", "Primitive tests", "Mipmap cells read",
                                             "Skipped squares",        "Cells visited",   "Runs tested",
                                             "GPU clock cycles"};
    ImGui::TextUnformatted(kNames[static_cast<std::size_t>(counter)]);
    ImGui::SameLine();
    ImGui::TextDisabled("rays per count (log)");
    const ImU32 missColor = IM_COL32(110, 140, 190, 255);
    const ImU32 hitColor = ImGui::ColorConvertFloat4ToU32(kHighlightColor);
    ImGui::SameLine();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(hitColor), "hit");
    ImGui::SameLine();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(missColor), "miss");

    const ImVec2 corner = ImGui::GetCursorScreenPos();
    const float width = std::max(80.0f, ImGui::GetContentRegionAvail().x);
    const float height = std::max(60.0f, ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing());
    ImGui::InvisibleButton("##histogram", {width, height});
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(corner, {corner.x + width, corner.y + height}, IM_COL32(25, 25, 25, 255));
    const std::size_t bars = last + 1;
    const float barWidth = width / static_cast<float>(bars);
    const double top = std::log10(1.0 + static_cast<double>(tallest));
    const auto heightOf = [&](std::uint64_t rays) {
        return static_cast<float>(std::log10(1.0 + static_cast<double>(rays)) / top) * (height - 2.0f);
    };
    const auto rangeOf = [](std::size_t k) {
        if (k == 0)
            return std::string("0");
        const std::uint64_t low = std::uint64_t{1} << (k - 1);
        const std::uint64_t high = (std::uint64_t{1} << k) - 1;
        return low == high ? std::to_string(low) : std::format("{}-{}", low, high);
    };
    for (std::size_t k = 0; k < bars; ++k) {
        const float left = corner.x + static_cast<float>(k) * barWidth + 1.0f;
        const float right = left + std::max(1.0f, barWidth - 2.0f);
        const float bottom = corner.y + height;
        // Stacked in log height: the whole bar is log(1 + all), the hit part its share of it.
        const float whole = heightOf(misses[k] + hits[k]);
        const double hitShare =
            misses[k] + hits[k] == 0 ? 0.0 : static_cast<double>(hits[k]) / static_cast<double>(misses[k] + hits[k]);
        const float hitHeight = whole * static_cast<float>(hitShare);
        draw->AddRectFilled({left, bottom - whole}, {right, bottom - hitHeight}, missColor);
        draw->AddRectFilled({left, bottom - hitHeight}, {right, bottom}, hitColor);
        if (hovered && ImGui::GetIO().MousePos.x >= left - 1.0f && ImGui::GetIO().MousePos.x < right + 1.0f)
            ImGui::SetTooltip("count %s\nrays with a hit: %llu\nrays without: %llu", rangeOf(k).c_str(),
                              static_cast<unsigned long long>(hits[k]), static_cast<unsigned long long>(misses[k]));
    }
    // The count at the left end, the middle and the right end of the axis.
    const auto axis = [&](std::size_t k, float x) {
        const std::string text = rangeOf(k);
        draw->AddText({x, corner.y + height + 1.0f}, ImGui::GetColorU32(ImGuiCol_TextDisabled), text.c_str());
    };
    axis(0, corner.x);
    if (bars > 2)
        axis(bars / 2, corner.x + static_cast<float>(bars / 2) * barWidth);
    if (bars > 1) {
        const std::string text = rangeOf(last);
        axis(last, corner.x + width - ImGui::CalcTextSize(text.c_str()).x);
    }
    ImGui::Dummy({width, ImGui::GetTextLineHeight()});
}

void ControlPanel::drawComparison()
{
    std::vector<std::string> choices;
    const bool surface = facts_.tracers.at(activeTracer_).findsHits;
    // "Mipmap off / on" needs the same bricks with skipping the other way (IEncoding::skippingTwin).
    const bool hasTwin = !facts_.encodings.empty() && !facts_.encodings.at(facts_.encoding).skippingTwin.empty();
    if (facts_.voxelCount && surface && hasTwin)
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
            ImGui::PushID(utf8(row.file).c_str());
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
                ImGui::SetClipboardText(utf8(std::filesystem::absolute(row.file)).c_str());
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
