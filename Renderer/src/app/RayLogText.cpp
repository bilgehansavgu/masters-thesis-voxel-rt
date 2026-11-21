#include "RayLogText.h"

#include "thesis/shared/Brick2012.h"
#include "thesis/shared/RayLog.h"

#include <format>

namespace thesis::app {
namespace {

const char* faceName(std::uint32_t face)
{
    constexpr const char* kNames[] = {"-x", "+x", "-y", "+y", "-z", "+z"};
    return face < 6 ? kNames[face] : "?";
}

const char* cellKindName(std::uint32_t kind)
{
    switch (kind) {
    case THESIS_CELL_EMPTY:
        return "empty";
    case THESIS_CELL_HEIGHT:
        return "one voxel";
    case THESIS_CELL_POINTER:
        return "runs";
    default:
        return "?";
    }
}

RayLogLine describe(const core::RayLogEntry& step, const StructureStepText& structureStep)
{
    RayLogLine line;
    line.t = step.t;
    switch (step.kind) {
    case THESIS_LOG_STRUCTURE_STEP:
        line.text = structureStep(step);
        break;
    case THESIS_LOG_PRIMITIVE:
        line.depth = 1;
        line.text = std::format("test primitive {}", step.a);
        break;
    case THESIS_LOG_CLOSER_HIT:
        line.depth = 1;
        line.hasT = true;
        line.important = true;
        line.text = std::format("primitive {} is the closest hit so far", step.a);
        break;
    case THESIS_LOG_RESULT:
        line.hasT = step.a != tracing::kNoLoggedRay;
        line.important = true;
        line.text = step.a == tracing::kNoLoggedRay
                        ? std::string("result: no hit")
                        : std::format("result: hit on primitive {}, face {}", step.a, faceName(step.b));
        break;
    case THESIS_LOG_MIPMAP: {
        line.depth = 2;
        line.hasT = true;
        line.important = step.f == 1;
        const std::uint32_t size = 1u << step.a;
        const std::string square =
            std::format("mipmap level {}: square of {}x{} cells from ({}, {})", step.a, size, size, step.b, step.c);
        if (step.d > step.e) {
            line.text = square + ", empty -> skip";
        } else {
            line.text = std::format("{}, voxels {} to {} -> {}", square, step.d, step.e,
                                    step.f == 1 ? "ray passes by, skip" : "may hit, one level down");
        }
        break;
    }
    case THESIS_LOG_CELL:
        line.depth = 2;
        line.hasT = true;
        line.text = step.c == THESIS_CELL_HEIGHT
                        ? std::format("cell ({}, {}): one voxel at height {}", step.a, step.b, step.d)
                        : std::format("cell ({}, {}): {}", step.a, step.b, cellKindName(step.c));
        break;
    case THESIS_LOG_RUN:
        line.depth = 3;
        line.hasT = true;
        line.text = std::format("run of {} voxels from height {}: {}", step.b, step.a,
                                step.c == 1 ? "the ray meets it" : "missed");
        break;
    case THESIS_LOG_VOXEL_HIT:
        line.depth = 2;
        line.hasT = true;
        line.important = true;
        line.text =
            std::format("hit: voxel at height {} of cell ({}, {}), face {}", step.c, step.a, step.b, faceName(step.d));
        break;
    default:
        line.text = std::format("unknown step {}", step.kind);
        break;
    }
    return line;
}

} // namespace

std::vector<RayLogLine> describeRayLog(const tracing::RayLog& log, const StructureStepText& structureStep)
{
    std::vector<RayLogLine> lines;
    lines.reserve(log.steps.size() + 1);
    for (const core::RayLogEntry& step : log.steps) {
        lines.push_back(describe(step, structureStep));
    }
    if (log.lostSteps > 0) {
        lines.push_back(
            {0, std::format("... {} more steps did not fit into the log", log.lostSteps), 0.0f, false, true});
    }
    return lines;
}

std::string rayLogAsText(const std::vector<RayLogLine>& lines)
{
    std::string text;
    for (const RayLogLine& line : lines) {
        text += std::string(static_cast<std::size_t>(2 * line.depth), ' ');
        text += line.text;
        if (line.hasT) {
            text += std::format("  (t = {:.6g})", line.t);
        }
        text += '\n';
    }
    return text;
}

} // namespace thesis::app
