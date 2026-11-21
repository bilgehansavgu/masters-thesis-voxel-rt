#pragma once

// Side: CPU

#include "thesis/core/GpuTypes.h"

#include <cstddef>
#include <span>

namespace thesis::core {

// How two hit lists for the same rays differ.
//
// Two hits agree when both miss, or both hit at the same distance (relative tolerance).
// Rounding can make a ray that grazes an edge hit in one tracer and miss in the other; that is
// why an agreement check allows a tiny fraction of disagreeing rays instead of demanding zero.
// A different primitive at the same distance (two surfaces touching) is counted separately:
// both answers are correct. The same holds for different attributes on the same primitive: two
// voxels of one brick that share an edge.
struct HitComparison {
    std::size_t rays = 0;
    std::size_t bothMiss = 0;
    std::size_t bothHit = 0;
    std::size_t hitVersusMiss = 0;     // one tracer hit, the other missed
    std::size_t distanceMismatch = 0;  // both hit, distances differ beyond tolerance
    std::size_t faceMismatch = 0;      // same primitive at the same distance, different side
    std::size_t primitiveMismatch = 0; // both hit at the same distance, different primitive
    std::size_t attributeMismatch = 0; // same primitive, distance and side, different attributes
    float maxRelativeDistanceError = 0.0f;

    [[nodiscard]] std::size_t disagreements() const { return hitVersusMiss + distanceMismatch + faceMismatch; }
    [[nodiscard]] double disagreementFraction() const
    {
        return rays == 0 ? 0.0 : static_cast<double>(disagreements()) / static_cast<double>(rays);
    }
    // The rule of the cross-tracer agreement test, also used by the fixed-view comparison.
    [[nodiscard]] bool agrees() const
    {
        return disagreementFraction() <= kMaxDisagreementFraction &&
               static_cast<double>(primitiveMismatch + attributeMismatch) <=
                   kMaxTouchingFraction * static_cast<double>(rays);
    }

    static constexpr float kRelativeTolerance = 1e-4f;
    static constexpr double kMaxDisagreementFraction = 1e-4; // at most 1 ray in 10,000
    static constexpr double kMaxTouchingFraction = 1e-3;     // touching surfaces: both answers are correct
};

[[nodiscard]] HitComparison compareHits(std::span<const Hit> a, std::span<const Hit> b,
                                        float relativeTolerance = HitComparison::kRelativeTolerance);

} // namespace thesis::core
