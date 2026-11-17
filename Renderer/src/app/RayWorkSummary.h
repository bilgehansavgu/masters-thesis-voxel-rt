#pragma once

#include "thesis/core/GpuTypes.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <span>

namespace thesis::app {

// The work counters of many rays, added up (core::RayWork, from the counting variant of a
// tracer). The benchmark writes it to the results file; the panel of the window shows it live
// for the frame on screen.
class RayWorkSummary {
public:
    // Mipmap levels 0 .. kLevels-1. Level L has squares of 2^L x 2^L cells.
    static constexpr std::size_t kLevels = 8;

    struct Means {
        double primitiveTests = 0.0;
        double cells = 0.0;
        double runs = 0.0;
        double structureSteps = 0.0;
        double mipmapCells = 0.0;
        double mipmapSkips = 0.0;
    };

    // Adds the rays of one frame. `work` and `hits` have one entry for each ray.
    void add(std::span<const core::RayWork> work, std::span<const core::Hit> hits);

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

    [[nodiscard]] nlohmann::json toJson() const;

private:
    // Sums over all rays [0] and over the rays with a hit [1].
    struct Sums {
        std::uint64_t primitiveTests = 0;
        std::uint64_t cells = 0;
        std::uint64_t runs = 0;
        std::uint64_t structureSteps = 0;
        std::uint64_t mipmapCells = 0;
        std::uint64_t mipmapSkips = 0;
    };
    [[nodiscard]] Means means(std::size_t group) const;

    std::uint64_t rays_ = 0;
    std::uint64_t raysWithHit_ = 0;
    std::array<Sums, 2> sums_{};
    core::RayWork most_{};
    std::array<std::uint64_t, kLevels> raysByLargestSkip_{};
    std::array<std::uint64_t, 4> hitCells_{};
};

} // namespace thesis::app
