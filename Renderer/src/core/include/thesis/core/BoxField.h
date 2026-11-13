#pragma once

#include "thesis/core/Scene.h"

#include <cstdint>

namespace thesis::core {

struct BoxFieldOptions {
    std::uint32_t gridSize = 20; // boxes per side
    float cellSize = 1.0f;
    std::uint32_t seed = 1;
};

// Step 0 placeholder scene: a ground slab and a grid of boxes with random heights, some with a
// small cube floating above. Boxes never touch each other (only the ground), so both tracers
// have a single correct answer almost everywhere. Deterministic across compilers and platforms
// for a given seed (own random generator, no std:: distributions).
[[nodiscard]] Scene makeBoxField(const BoxFieldOptions& options = {});

} // namespace thesis::core
