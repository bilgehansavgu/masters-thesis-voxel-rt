#pragma once

// Side: CPU

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>

namespace thesis::metrics {

// Conditions that change benchmark numbers on a laptop, captured at one moment:
//   power:  on charger or battery, Windows power plan and power mode
//   gpu:    clocks, temperature, power draw and power limit, performance state (NVIDIA, via NVML)
// Fields that cannot be read are reported as unavailable rather than guessed.
[[nodiscard]] nlohmann::json captureSystemState();

// The "gpu" part of captureSystemState() alone: cheap enough to record after each repeat.
[[nodiscard]] nlohmann::json captureGpuState();

// The GPU's current graphics clock, if it can be read (NVIDIA only). Cheap enough to poll.
[[nodiscard]] std::optional<std::uint32_t> currentGraphicsClockMhz();

// Which code produced a result: git commit (captured at build time), configuration, compiler.
[[nodiscard]] nlohmann::json buildInfo();

} // namespace thesis::metrics
