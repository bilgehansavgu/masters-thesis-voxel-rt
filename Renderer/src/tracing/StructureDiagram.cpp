#include "thesis/tracing/StructureDiagram.h"

#include "thesis/shared/RayLog.h"

namespace thesis::tracing {

std::pair<std::uint32_t, std::size_t> nextStructureEvent(std::span<const core::RayLogEntry> steps, std::size_t i)
{
    for (std::size_t j = i + 1; j < steps.size(); ++j) {
        const auto kind = steps[j].kind;
        if (kind != THESIS_LOG_CLOSER_HIT && kind != THESIS_LOG_MIPMAP && kind != THESIS_LOG_CELL &&
            kind != THESIS_LOG_RUN && kind != THESIS_LOG_VOXEL_HIT)
            return {kind, j};
    }
    return {std::uint32_t(THESIS_LOG_RESULT), steps.size()};
}

} // namespace thesis::tracing
