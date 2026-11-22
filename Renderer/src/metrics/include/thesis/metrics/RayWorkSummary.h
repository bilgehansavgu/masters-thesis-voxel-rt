#pragma once

// Side: CPU

#include "thesis/core/GpuTypes.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace thesis::metrics {

// One counter of core::RayWork that the summary keeps statistics of.
enum class WorkCounter : std::uint32_t {
    StructureSteps,
    PrimitiveTests,
    MipmapCells,
    MipmapSkips,
    Cells,
    Runs,
    Cycles,
};
inline constexpr std::size_t kWorkCounters = 7;

// The value of `counter` in `work`.
[[nodiscard]] std::uint32_t workValue(const core::RayWork& work, WorkCounter counter);
// The key of a counter in results files: "structure_steps", ..., "cycles".
[[nodiscard]] const char* workKey(WorkCounter counter);

// The work counters of many rays, added up (core::RayWork, from the counting variant of a
// tracer). The benchmark writes it to the results file; the panel of the window shows it live
// for the frame on screen.
//
// Besides the means it keeps, for each counter:
//   - a histogram, separate for rays with and without a hit. Bins are exact up to 63, then 32
//     bins for each power of two (about 3 % wide), so percentiles are close for large counts too.
//   - the ray with the largest count, for "the costliest ray" in the window.
//   - the warp use, when the rays come in warps: the work of all rays divided by the work their
//     warps were busy for (warp size x the largest count of each warp). 100 % means that no ray of
//     a warp waited for another one.
class RayWorkSummary {
public:
    // Mipmap levels 0 .. kLevels-1. Level L has squares of 2^L x 2^L cells.
    static constexpr std::size_t kLevels = 8;
    static constexpr std::size_t kBins = 896;
    // Octave buckets for display: [0] the count 0, [k] counts in [2^(k-1), 2^k).
    static constexpr std::size_t kOctaves = 34;

    struct Means {
        double primitiveTests = 0.0;
        double cells = 0.0;
        double runs = 0.0;
        double structureSteps = 0.0;
        double mipmapCells = 0.0;
        double mipmapSkips = 0.0;
        double cycles = 0.0;
    };

    // Adds the rays of one frame. `work` and `hits` have one entry for each ray, in the order of
    // the ray list. `warpSize`: rays [k * warpSize, (k + 1) * warpSize) ran as one warp; 0: the
    // tracer places rays on warps in its own way, so no warp use is kept. Every call of one
    // summary passes the same warp size (std::logic_error otherwise).
    void add(std::span<const core::RayWork> work, std::span<const core::Hit> hits, std::uint32_t warpSize = 0);

    [[nodiscard]] std::uint64_t rays() const { return rays_; }
    [[nodiscard]] std::uint64_t raysWithHit() const { return raysWithHit_; }
    [[nodiscard]] Means meanPerRay() const { return means(0); }
    [[nodiscard]] Means meanPerRayWithHit() const { return means(1); }
    // The largest count of any ray, for each counter.
    [[nodiscard]] const core::RayWork& most() const { return most_; }
    // Rays by the largest mipmap square that the walk skipped for them. [0]: no square skipped.
    [[nodiscard]] const std::array<std::uint64_t, kLevels>& raysByLargestSkip() const { return raysByLargestSkip_; }
    // Rays with a hit, by the kind of the cell with the hit (THESIS_HIT_CELL_*).
    [[nodiscard]] const std::array<std::uint64_t, 4>& hitsByCellKind() const { return hitCells_; }

    // The index in the ray list of the first ray with the largest count. Meaningful for a summary
    // of one frame (the window); the benchmark adds several.
    [[nodiscard]] std::uint32_t costliestRay(WorkCounter counter) const;
    // The count that a share `q` (0 to 1) of the rays with a count above 0 stay at or below.
    // 0 if no ray has a count above 0. Exact up to 63, within about 3 % above.
    [[nodiscard]] double percentile(WorkCounter counter, double q) const;
    // Rays by octave of the count (kOctaves buckets), of the rays with a hit or without one.
    [[nodiscard]] std::array<std::uint64_t, kOctaves> octaves(WorkCounter counter, bool withHit) const;
    // Work of all rays / (warp size x largest count of each warp), 0 to 1. None without warps,
    // without any work of this counter, and for the cycles (100 % by construction, see the .cpp).
    [[nodiscard]] std::optional<double> warpUse(WorkCounter counter) const;
    [[nodiscard]] std::uint32_t warpSize() const { return warpSize_; }

    // `unavailable`: counters the tracer does not count. They are null in every group of the
    // result (means, maximum, percentiles, warp use), so that no 0 reads as a measured value.
    [[nodiscard]] nlohmann::json toJson(std::span<const WorkCounter> unavailable = {}) const;

    // The histogram bin of a count, and the smallest count of a bin. Public for the tests.
    [[nodiscard]] static std::size_t binOf(std::uint32_t count);
    [[nodiscard]] static std::uint64_t binStart(std::size_t bin);

private:
    struct CounterStats {
        std::array<std::uint64_t, 2> sums{};                    // all rays [0], rays with a hit [1]
        std::array<std::array<std::uint64_t, kBins>, 2> bins{}; // rays without [0] and with [1] a hit
        std::uint32_t most = 0;
        std::uint32_t mostRay = 0;
        std::uint64_t warpWork = 0; // the sum over warps of (rays of the warp x its largest count)
    };
    [[nodiscard]] Means means(std::size_t group) const;

    std::uint64_t rays_ = 0;
    std::uint64_t raysWithHit_ = 0;
    std::uint32_t warpSize_ = 0;
    std::array<CounterStats, kWorkCounters> counters_{};
    core::RayWork most_{};
    std::array<std::uint64_t, kLevels> raysByLargestSkip_{};
    std::array<std::uint64_t, 4> hitCells_{};
};

} // namespace thesis::metrics
