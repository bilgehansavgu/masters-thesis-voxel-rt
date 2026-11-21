#include "StructureView.h"

#include "thesis/shared/RayLog.h"

#include <imgui.h>

#include <algorithm>
#include <format>
#include <limits>

namespace thesis::app {

namespace {

using Kind = tracing::CutRect::Kind;

constexpr ImU32 kEmptyColour = IM_COL32(150, 165, 175, 150);
constexpr ImU32 kFullColour = IM_COL32(80, 160, 255, 235);
constexpr ImU32 kGroupColour = IM_COL32(255, 140, 70, 235);
constexpr ImU32 kGridColour = IM_COL32(150, 165, 175, 110);
// More rectangles than this make the image unreadable and the frame slow.
constexpr std::size_t kMaxRects = 6000;

ImU32 colourOf(Kind kind)
{
    switch (kind) {
    case Kind::Empty:
        return kEmptyColour;
    case Kind::Full:
        return kFullColour;
    case Kind::Group:
        return kGroupColour;
    case Kind::Line:
        return kGridColour;
    }
    return kFullColour;
}

} // namespace

tracing::IStructureDiagram* StructureView::diagram(std::size_t tracer, const DiagramFactory& make,
                                                   const LessonGeometry& scene, const core::Bounds& bounds)
{
    if (tracer >= diagrams_.size()) {
        diagrams_.resize(tracer + 1);
        made_.resize(tracer + 1, false);
        details_.resize(tracer + 1, 0);
    }
    if (!made_[tracer]) {
        made_[tracer] = true;
        if (make) {
            diagrams_[tracer] = make({scene.bounds, bounds, scene.grid});
            if (diagrams_[tracer]) {
                if (const auto detail = diagrams_[tracer]->detail())
                    details_[tracer] = detail->initial;
            }
        }
    }
    return diagrams_[tracer].get();
}

void StructureView::controls(std::size_t tracer, const DiagramFactory& make, const LessonGeometry& scene,
                             const core::Bounds& bounds)
{
    tracer_ = tracer;
    ImGui::Checkbox("Show the search structure", &visible_);
    if (!visible_)
        return;
    tracing::IStructureDiagram* shown = diagram(tracer, make, scene, bounds);
    if (!shown) {
        ImGui::TextDisabled("This method has no search structure.");
        return;
    }
    ImGui::SliderInt("Cut height", &cutPercent_, 0, 100, "%d %%");
    int& detail = details_[tracer];
    if (const auto spec = shown->detail())
        ImGui::SliderInt(spec->name.c_str(), &detail, spec->min, spec->max);
    if (cutTracer_ != tracer || cutPercentBuilt_ != cutPercent_ || detailBuilt_ != detail) {
        cutTracer_ = tracer;
        cutPercentBuilt_ = cutPercent_;
        detailBuilt_ = detail;
        cutY_ = bounds.lo.y + (bounds.hi.y - bounds.lo.y) * (static_cast<float>(cutPercent_) / 100.0f);
        cut_ = shown->cut(cutY_, detail);
    }

    ImGui::TextWrapped("%s", cut_.text.c_str());
    for (const auto& entry : cut_.legend) {
        // A small square in the colour of the lines; the UI font has no square glyph.
        const float size = ImGui::GetTextLineHeight() * 0.7f;
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float inset = (ImGui::GetTextLineHeight() - size) * 0.5f;
        ImGui::GetWindowDrawList()->AddRect({at.x, at.y + inset}, {at.x + size, at.y + inset + size},
                                            colourOf(entry.kind), 0.0f, 0, 2.0f);
        ImGui::Dummy({size, ImGui::GetTextLineHeight()});
        ImGui::SameLine();
        ImGui::TextUnformatted(entry.text.c_str());
    }
    if (cut_.rects.size() > kMaxRects)
        ImGui::TextDisabled("Only the first %zu squares are drawn.", kMaxRects);
}

void StructureView::draw(const OverlayPainter& painter) const
{
    if (!visible_ || cutTracer_ != tracer_)
        return;
    if (const auto& grid = cut_.gridLines) {
        // The lines of every cell of the cut; the cells that hold a brick are drawn on top.
        const glm::vec2 size = glm::vec2(grid->count) * grid->cellSize;
        for (std::uint32_t x = 0; x <= grid->count.x; ++x) {
            const float at = grid->origin.x + static_cast<float>(x) * grid->cellSize.x;
            painter.line({at, cutY_, grid->origin.y}, {at, cutY_, grid->origin.y + size.y}, kGridColour, 1.0f);
        }
        for (std::uint32_t z = 0; z <= grid->count.y; ++z) {
            const float at = grid->origin.y + static_cast<float>(z) * grid->cellSize.y;
            painter.line({grid->origin.x, cutY_, at}, {grid->origin.x + size.x, cutY_, at}, kGridColour, 1.0f);
        }
    }
    const std::size_t drawn = std::min(cut_.rects.size(), kMaxRects);
    for (const Kind kind : {Kind::Empty, Kind::Group, Kind::Full}) {
        for (std::size_t i = 0; i < drawn; ++i) {
            const tracing::CutRect& rect = cut_.rects[i];
            if (rect.kind == kind)
                painter.flatRect(rect.lo, rect.hi, cutY_, colourOf(kind), kind == Kind::Empty ? 1.0f : 1.5f);
        }
    }
}

std::optional<RayStage> StructureView::searchStep(const tracing::RayLog& log, std::size_t tracer,
                                                  const DiagramFactory& make, const LessonGeometry& scene,
                                                  const core::Bounds& bounds, const PrimaryRay& ray)
{
    tracing::IStructureDiagram* structure = diagram(tracer, make, scene, bounds);
    if (!structure)
        return std::nullopt;
    structure->beginWalk();
    const tracing::WalkText text = structure->walkText();
    RayStage stage{"Traversal", "", text.tooltip, {}, false, true};
    const auto& steps = log.steps;
    std::size_t nodes = 0, tests = 0;
    float closest = std::numeric_limits<float>::infinity();

    // The result of the brick test that starts at steps[i] (a PRIMITIVE event): the distance of a
    // closer hit, if the test found one before the next primitive or structure step.
    const auto testResult = [&](std::size_t i) -> std::optional<float> {
        for (std::size_t j = i + 1; j < steps.size(); ++j) {
            const auto kind = steps[j].kind;
            if (kind == THESIS_LOG_CLOSER_HIT && steps[j].a == steps[i].a)
                return steps[j].t;
            if (kind == THESIS_LOG_PRIMITIVE || kind == THESIS_LOG_STRUCTURE_STEP || kind == THESIS_LOG_RESULT)
                break;
        }
        return std::nullopt;
    };

    for (std::size_t i = 0; i < steps.size(); ++i) {
        const auto& e = steps[i];
        RayEvent event;
        if (e.kind == THESIS_LOG_CLOSER_HIT) {
            closest = std::min(closest, e.t);
            continue;
        }
        if (e.kind == THESIS_LOG_PRIMITIVE) {
            ++tests;
            if (e.a < scene.bounds.size())
                event.boxes.push_back(scene.bounds[e.a]);
            const auto hit = testResult(i);
            event.positive = hit.has_value();
            event.t = hit;
            event.label = hit ? std::format("{} · primitive {} · hit, t = {:.4g}", text.brickTest, e.a, *hit)
                              : std::format("{} · primitive {} · no hit", text.brickTest, e.a);
        } else if (auto step = structure->walkStep(steps, i, ray.origin, ray.direction, closest)) {
            ++nodes;
            event.label = std::move(step->label);
            event.boxes = std::move(step->boxes);
            event.t = step->t;
            event.positive = step->positive;
        } else {
            continue;
        }
        stage.events.push_back(std::move(event));
    }

    stage.summary = text.stepName.empty()
                        ? std::format("internal traversal not observable · {} candidate bricks", tests)
                        : std::format("{} {} · {} brick tests", nodes, text.stepName, tests);
    if (log.lostSteps)
        stage.summary += " · log incomplete";
    if (stage.events.empty())
        stage.events.push_back({"no traversal events recorded"});
    return stage;
}

} // namespace thesis::app
