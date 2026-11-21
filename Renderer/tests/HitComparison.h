#pragma once

// Side: CPU

#include "thesis/core/GpuTypes.h"
#include "thesis/core/HitComparison.h"

#include <cstdint>
#include <filesystem>
#include <span>

namespace thesis::tests {

// The comparison lives in thesis::core, so that the fixed-view comparison of the window uses the
// same rule as the agreement tests.
using core::compareHits;
using core::HitComparison;

// Writes a hit list as a binary PPM image (misses black, hits grey by distance, near = bright),
// so a failing comparison can be looked at.
void writeHitImage(const std::filesystem::path& path, std::span<const core::Hit> hits, std::uint32_t width,
                   std::uint32_t height);

} // namespace thesis::tests
