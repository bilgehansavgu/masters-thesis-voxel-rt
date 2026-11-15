#pragma once

// ============================================================================
// READING ORDER: FILE 10 (Step 0 procedural test scene generator)
// ============================================================================

#include "thesis/core/Scene.h"

#include <cstdint>

namespace thesis::core {

struct BoxFieldOptions {
    std::uint32_t gridSize = 20; // boxes per side (20x20 grid)
    float cellSize = 1.0f;       // world-space width/depth of each grid cell
    std::uint32_t seed = 1;      // random number generator seed for reproducible scenes
};

// Step 0 placeholder scene: a ground slab and a grid of boxes with random heights, some with a
// small cube floating above. Boxes never touch each other (only the ground), so both tracers
// have a single correct answer almost everywhere. Deterministic across compilers and platforms
// for a given seed (own random generator, no std:: distributions).
[[nodiscard]] Scene makeBoxField(const BoxFieldOptions& options = {});

} // namespace thesis::core
