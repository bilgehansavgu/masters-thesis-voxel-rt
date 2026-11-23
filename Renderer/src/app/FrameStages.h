#pragma once

// Side: CPU

namespace thesis::app {

// The names of the stages of a frame (FramePipeline): shown in Tracy, used as keys in results
// files, and read by the window to sort the timings it gets.
inline constexpr const char* kStageRayGeneration = "ray_generation";
inline constexpr const char* kStageTrace = "trace";
inline constexpr const char* kStageShading = "shading";

} // namespace thesis::app
