#include "thesis/metrics/RayWorkSummary.h"

#include "thesis/shared/Hit.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace thesis::metrics {
namespace {

constexpr std::array<WorkCounter, kWorkCounters> kCounters{
    WorkCounter::StructureSteps, WorkCounter::PrimitiveTests, WorkCounter::MipmapCells, WorkCounter::MipmapSkips,
    WorkCounter::Cells,          WorkCounter::Runs,           WorkCounter::Cycles};

constexpr std::size_t kExactBins = 64; // counts 0 .. 63 have a bin each
constexpr std::size_t kBinsPerOctave = 32;

} // namespace

std::uint32_t workValue(const core::RayWork& work, WorkCounter counter)
{
    switch (counter) {
    case WorkCounter::StructureSteps:
        return work.structureSteps;
    case WorkCounter::PrimitiveTests:
        return work.primitiveTests;
    case WorkCounter::MipmapCells:
        return work.mipmapCells;
    case WorkCounter::MipmapSkips:
        return work.mipmapSkips;
    case WorkCounter::Cells:
        return work.cells;
    case WorkCounter::Runs:
        return work.runs;
    case WorkCounter::Cycles:
        return work.cycles;
    }
    throw std::logic_error("workValue: unknown counter");
}

const char* workKey(WorkCounter counter)
{
    switch (counter) {
    case WorkCounter::StructureSteps:
        return "structure_steps";
    case WorkCounter::PrimitiveTests:
        return "primitive_tests";
    case WorkCounter::MipmapCells:
        return "mipmap_cells";
    case WorkCounter::MipmapSkips:
        return "mipmap_skips";
    case WorkCounter::Cells:
        return "cells";
    case WorkCounter::Runs:
        return "runs";
    case WorkCounter::Cycles:
        return "cycles";
    }
    throw std::logic_error("workKey: unknown counter");
}

std::size_t RayWorkSummary::binOf(std::uint32_t count)
{
    if (count < kExactBins)
        return count;
    // A count with b significant bits: the top 6 bits pick one of 32 bins of its octave.
    const auto shift = static_cast<std::uint32_t>(std::bit_width(count)) - 6u;
    return kExactBins + (shift - 1u) * kBinsPerOctave + ((count >> shift) - kBinsPerOctave);
}

std::uint64_t RayWorkSummary::binStart(std::size_t bin)
{
    if (bin < kExactBins)
        return bin;
    const std::size_t shift = (bin - kExactBins) / kBinsPerOctave + 1;
    const std::uint64_t top = (bin - kExactBins) % kBinsPerOctave + kBinsPerOctave;
    return top << shift;
}

void RayWorkSummary::add(std::span<const core::RayWork> work, std::span<const core::Hit> hits, std::uint32_t warpSize)
{
    const std::size_t count = std::min(work.size(), hits.size());
    if (rays_ > 0 && warpSize != warpSize_)
        throw std::logic_error("RayWorkSummary::add: every frame of one summary needs the same warp size");
    warpSize_ = warpSize;
    for (std::size_t ray = 0; ray < count; ++ray) {
        const core::RayWork& w = work[ray];
        const bool hit = hits[ray].primitive != core::kNoHit;
        for (std::size_t c = 0; c < kWorkCounters; ++c) {
            const std::uint32_t value = workValue(w, kCounters[c]);
            CounterStats& stats = counters_[c];
            stats.sums[0] += value;
            if (hit)
                stats.sums[1] += value;
            ++stats.bins[hit ? 1 : 0][binOf(value)];
            if (value > stats.most) {
                stats.most = value;
                stats.mostRay = static_cast<std::uint32_t>(ray);
            }
        }
        most_.primitiveTests = std::max(most_.primitiveTests, w.primitiveTests);
        most_.cells = std::max(most_.cells, w.cells);
        most_.runs = std::max(most_.runs, w.runs);
        most_.structureSteps = std::max(most_.structureSteps, w.structureSteps);
        most_.mipmapCells = std::max(most_.mipmapCells, w.mipmapCells);
        most_.mipmapSkips = std::max(most_.mipmapSkips, w.mipmapSkips);
        most_.cycles = std::max(most_.cycles, w.cycles);
        most_.skippedLevels |= w.skippedLevels;

        const std::size_t largest =
            w.skippedLevels == 0 ? 0 : static_cast<std::size_t>(std::bit_width(w.skippedLevels) - 1);
        ++raysByLargestSkip_[std::min(largest, kLevels - 1)];
        ++hitCells_[std::min<std::uint32_t>(w.hitCell, THESIS_HIT_CELL_SEVERAL_RUNS)];
        raysWithHit_ += hit ? 1 : 0;
    }
    // The warps: each is busy for (its rays x its largest count).
    if (warpSize > 0) {
        for (std::size_t first = 0; first < count; first += warpSize) {
            const std::size_t end = std::min<std::size_t>(first + warpSize, count);
            for (std::size_t c = 0; c < kWorkCounters; ++c) {
                std::uint32_t largest = 0;
                for (std::size_t ray = first; ray < end; ++ray)
                    largest = std::max(largest, workValue(work[ray], kCounters[c]));
                counters_[c].warpWork += static_cast<std::uint64_t>(largest) * (end - first);
            }
        }
    }
    rays_ += count;
}

RayWorkSummary::Means RayWorkSummary::means(std::size_t group) const
{
    const std::uint64_t count = group == 0 ? rays_ : raysWithHit_;
    if (count == 0) {
        return {};
    }
    const auto mean = [&](WorkCounter counter) {
        return static_cast<double>(counters_[static_cast<std::size_t>(counter)].sums[group]) /
               static_cast<double>(count);
    };
    Means result;
    result.primitiveTests = mean(WorkCounter::PrimitiveTests);
    result.cells = mean(WorkCounter::Cells);
    result.runs = mean(WorkCounter::Runs);
    result.structureSteps = mean(WorkCounter::StructureSteps);
    result.mipmapCells = mean(WorkCounter::MipmapCells);
    result.mipmapSkips = mean(WorkCounter::MipmapSkips);
    result.cycles = mean(WorkCounter::Cycles);
    return result;
}

std::uint32_t RayWorkSummary::costliestRay(WorkCounter counter) const
{
    return counters_[static_cast<std::size_t>(counter)].mostRay;
}

double RayWorkSummary::percentile(WorkCounter counter, double q) const
{
    const CounterStats& stats = counters_[static_cast<std::size_t>(counter)];
    std::uint64_t total = 0;
    for (std::size_t bin = 1; bin < kBins; ++bin)
        total += stats.bins[0][bin] + stats.bins[1][bin];
    if (total == 0)
        return 0.0;
    const auto target = static_cast<std::uint64_t>(std::ceil(std::clamp(q, 0.0, 1.0) * static_cast<double>(total)));
    std::uint64_t seen = 0;
    for (std::size_t bin = 1; bin < kBins; ++bin) {
        seen += stats.bins[0][bin] + stats.bins[1][bin];
        if (seen >= std::max<std::uint64_t>(target, 1)) {
            // The largest count of the bin: the share stays at or below it.
            return static_cast<double>(bin + 1 < kBins ? binStart(bin + 1) - 1
                                                       : std::uint64_t{std::numeric_limits<std::uint32_t>::max()});
        }
    }
    return static_cast<double>(stats.most);
}

std::array<std::uint64_t, RayWorkSummary::kOctaves> RayWorkSummary::octaves(WorkCounter counter, bool withHit) const
{
    std::array<std::uint64_t, kOctaves> result{};
    const auto& bins = counters_[static_cast<std::size_t>(counter)].bins[withHit ? 1 : 0];
    for (std::size_t bin = 0; bin < kBins; ++bin) {
        const std::uint64_t start = binStart(bin);
        const std::size_t octave = start == 0 ? 0 : static_cast<std::size_t>(std::bit_width(start));
        result[std::min(octave, kOctaves - 1)] += bins[bin];
    }
    return result;
}

std::optional<double> RayWorkSummary::warpUse(WorkCounter counter) const
{
    // WHY none for the cycles: the threads of a warp read the clock together at the start and,
    // after they meet again, at the end, so every ray of a warp has the cycles of the warp and
    // the use is 100 % by construction, whatever the rays did.
    const CounterStats& stats = counters_[static_cast<std::size_t>(counter)];
    if (warpSize_ == 0 || stats.warpWork == 0 || counter == WorkCounter::Cycles)
        return std::nullopt;
    return static_cast<double>(stats.sums[0]) / static_cast<double>(stats.warpWork);
}

nlohmann::json RayWorkSummary::toJson(std::span<const WorkCounter> unavailable) const
{
    const auto meansJson = [](const Means& m) {
        return nlohmann::json{{"primitive_tests", m.primitiveTests},
                              {"cells", m.cells},
                              {"runs", m.runs},
                              {"structure_steps", m.structureSteps},
                              {"mipmap_cells", m.mipmapCells},
                              {"mipmap_skips", m.mipmapSkips},
                              {"cycles", m.cycles}};
    };
    nlohmann::json bySkip = nlohmann::json::object();
    for (std::size_t level = 0; level < kLevels; ++level) {
        if (raysByLargestSkip_[level] > 0) {
            bySkip[level == 0 ? std::string("none") : "level_" + std::to_string(level)] = raysByLargestSkip_[level];
        }
    }
    nlohmann::json percentiles = nlohmann::json::object();
    nlohmann::json warp = nlohmann::json::object();
    for (const WorkCounter counter : kCounters) {
        percentiles[workKey(counter)] = {
            {"p50", percentile(counter, 0.5)}, {"p90", percentile(counter, 0.9)}, {"p99", percentile(counter, 0.99)}};
        if (const std::optional<double> use = warpUse(counter))
            warp[workKey(counter)] = *use;
    }
    nlohmann::json result = {{"rays", rays_},
                             {"rays_with_hit", raysWithHit_},
                             {"mean_per_ray", meansJson(meanPerRay())},
                             {"mean_per_ray_with_hit", meansJson(meanPerRayWithHit())},
                             {"max_per_ray",
                              {{"primitive_tests", most_.primitiveTests},
                               {"cells", most_.cells},
                               {"runs", most_.runs},
                               {"structure_steps", most_.structureSteps},
                               {"mipmap_cells", most_.mipmapCells},
                               {"mipmap_skips", most_.mipmapSkips},
                               {"cycles", most_.cycles}}},
                             {"percentiles_of_rays_with_work", percentiles},
                             {"rays_by_largest_skipped_square", bySkip},
                             {"hits_by_cell_kind",
                              {{"height", hitCells_[THESIS_HIT_CELL_HEIGHT]},
                               {"one_run", hitCells_[THESIS_HIT_CELL_ONE_RUN]},
                               {"several_runs", hitCells_[THESIS_HIT_CELL_SEVERAL_RUNS]}}}};
    if (std::find(unavailable.begin(), unavailable.end(), WorkCounter::Cycles) == unavailable.end())
        result["cycles_note"] = "GPU clock cycles of the warp, from the counting shader: not a timing";
    if (warpSize_ > 0) {
        result["warp_size"] = warpSize_;
        result["warp_use"] = warp;
    }
    for (const WorkCounter counter : unavailable) {
        for (nlohmann::json& group : result) {
            if (group.is_object() && group.contains(workKey(counter)))
                group[workKey(counter)] = nullptr;
        }
    }
    return result;
}

} // namespace thesis::metrics
