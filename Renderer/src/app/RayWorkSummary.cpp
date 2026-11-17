#include "RayWorkSummary.h"

#include "thesis/shared/Hit.h"

#include <algorithm>
#include <bit>

namespace thesis::app {

void RayWorkSummary::add(std::span<const core::RayWork> work, std::span<const core::Hit> hits)
{
    const std::size_t count = std::min(work.size(), hits.size());
    for (std::size_t ray = 0; ray < count; ++ray) {
        const core::RayWork& w = work[ray];
        const bool hit = hits[ray].primitive != core::kNoHit;
        for (std::size_t group = 0; group < (hit ? 2u : 1u); ++group) {
            Sums& sums = sums_[group];
            sums.primitiveTests += w.primitiveTests;
            sums.cells += w.cells;
            sums.runs += w.runs;
            sums.structureSteps += w.structureSteps;
            sums.mipmapCells += w.mipmapCells;
            sums.mipmapSkips += w.mipmapSkips;
        }
        most_.primitiveTests = std::max(most_.primitiveTests, w.primitiveTests);
        most_.cells = std::max(most_.cells, w.cells);
        most_.runs = std::max(most_.runs, w.runs);
        most_.structureSteps = std::max(most_.structureSteps, w.structureSteps);
        most_.mipmapCells = std::max(most_.mipmapCells, w.mipmapCells);
        most_.mipmapSkips = std::max(most_.mipmapSkips, w.mipmapSkips);
        most_.skippedLevels |= w.skippedLevels;

        const std::size_t largest =
            w.skippedLevels == 0 ? 0 : static_cast<std::size_t>(std::bit_width(w.skippedLevels) - 1);
        ++raysByLargestSkip_[std::min(largest, kLevels - 1)];
        ++hitCells_[std::min<std::uint32_t>(w.hitCell, THESIS_HIT_CELL_SEVERAL_RUNS)];
        raysWithHit_ += hit ? 1 : 0;
    }
    rays_ += count;
}

RayWorkSummary::Means RayWorkSummary::means(std::size_t group) const
{
    const std::uint64_t count = group == 0 ? rays_ : raysWithHit_;
    if (count == 0) {
        return {};
    }
    const auto mean = [&](std::uint64_t sum) { return static_cast<double>(sum) / static_cast<double>(count); };
    const Sums& sums = sums_[group];
    return {mean(sums.primitiveTests), mean(sums.cells),       mean(sums.runs),
            mean(sums.structureSteps), mean(sums.mipmapCells), mean(sums.mipmapSkips)};
}

nlohmann::json RayWorkSummary::toJson() const
{
    const auto meansJson = [](const Means& m) {
        return nlohmann::json{{"primitive_tests", m.primitiveTests},
                              {"cells", m.cells},
                              {"runs", m.runs},
                              {"structure_steps", m.structureSteps},
                              {"mipmap_cells", m.mipmapCells},
                              {"mipmap_skips", m.mipmapSkips}};
    };
    nlohmann::json bySkip = nlohmann::json::object();
    for (std::size_t level = 0; level < kLevels; ++level) {
        if (raysByLargestSkip_[level] > 0) {
            bySkip[level == 0 ? std::string("none") : "level_" + std::to_string(level)] = raysByLargestSkip_[level];
        }
    }
    return {{"rays", rays_},
            {"rays_with_hit", raysWithHit_},
            {"mean_per_ray", meansJson(meanPerRay())},
            {"mean_per_ray_with_hit", meansJson(meanPerRayWithHit())},
            {"max_per_ray",
             {{"primitive_tests", most_.primitiveTests},
              {"cells", most_.cells},
              {"runs", most_.runs},
              {"structure_steps", most_.structureSteps},
              {"mipmap_cells", most_.mipmapCells},
              {"mipmap_skips", most_.mipmapSkips}}},
            {"rays_by_largest_skipped_square", bySkip},
            {"hits_by_cell_kind",
             {{"height", hitCells_[THESIS_HIT_CELL_HEIGHT]},
              {"one_run", hitCells_[THESIS_HIT_CELL_ONE_RUN]},
              {"several_runs", hitCells_[THESIS_HIT_CELL_SEVERAL_RUNS]}}}};
}

} // namespace thesis::app
