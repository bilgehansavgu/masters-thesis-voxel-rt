#pragma once

#include "thesis/core/GpuTypes.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>

namespace thesis::tests {

// How two hit lists for the same rays differ.
//
// Two hits agree when both miss, or both hit at the same distance (relative tolerance).
// Rounding can make a ray that grazes an edge hit in one tracer and miss in the other; that is
// why the agreement test allows a tiny fraction of disagreeing rays instead of demanding zero.
// A different primitive at the same distance (two surfaces touching) is counted separately:
// both answers are correct.
struct HitComparison {
    std::size_t rays = 0;
    std::size_t bothMiss = 0;
    std::size_t bothHit = 0;
    std::size_t hitVersusMiss = 0;     // one tracer hit, the other missed
    std::size_t distanceMismatch = 0;  // both hit, distances differ beyond tolerance
    std::size_t faceMismatch = 0;      // same primitive at the same distance, different side
    std::size_t primitiveMismatch = 0; // both hit at the same distance, different primitive
    float maxRelativeDistanceError = 0.0f;

    [[nodiscard]] std::size_t disagreements() const { return hitVersusMiss + distanceMismatch + faceMismatch; }
    [[nodiscard]] double disagreementFraction() const
    {
        return rays == 0 ? 0.0 : static_cast<double>(disagreements()) / static_cast<double>(rays);
    }
};

[[nodiscard]] HitComparison compareHits(std::span<const core::Hit> a, std::span<const core::Hit> b,
                                        float relativeTolerance);

// Writes a hit list as a binary PPM image (misses black, hits grey by distance, near = bright),
// so a failing comparison can be looked at.
void writeHitImage(const std::filesystem::path& path, std::span<const core::Hit> hits, std::uint32_t width,
                   std::uint32_t height);

} // namespace thesis::tests
