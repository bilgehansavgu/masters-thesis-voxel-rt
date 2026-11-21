#include "thesis/tracing/RayLog.h"

namespace thesis::tracing {

core::RayWork countWork(const RayLog& log)
{
    core::RayWork work{};
    for (const core::RayLogEntry& step : log.steps) {
        switch (step.kind) {
        case THESIS_LOG_STRUCTURE_STEP:
            ++work.structureSteps;
            break;
        case THESIS_LOG_PRIMITIVE:
            ++work.primitiveTests;
            break;
        case THESIS_LOG_MIPMAP:
            ++work.mipmapCells;
            if (step.f == 1) {
                ++work.mipmapSkips;
                work.skippedLevels |= 1u << step.a;
            }
            break;
        case THESIS_LOG_CELL:
            ++work.cells;
            break;
        case THESIS_LOG_RUN:
            ++work.runs;
            break;
        default:
            break;
        }
    }
    return work;
}

} // namespace thesis::tracing
