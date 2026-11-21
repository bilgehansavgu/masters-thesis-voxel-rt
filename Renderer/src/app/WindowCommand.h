#pragma once

// Side: CPU

#include "CommandLine.h"
#include "Options.h"

#include "thesis/core/Camera.h"
#include "thesis/resolve/ShadingParams.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>

namespace thesis::app {

// A camera of the benchmark plans (scripts/rerun_step3.json): yaw and pitch in degrees, and the
// distance as a factor of the distance that shows the whole scene (--camera).
struct CameraPreset {
    const char* name;
    std::array<float, 3> camera; // yaw, pitch, factor
};

// WHY no factor below 0.18: at 0.12 the camera is inside the Lucy statue, so the view is invalid.
inline constexpr std::array<CameraPreset, 3> kCameraPresets{{
    {"standard", {35.0f, 30.0f, 1.0f}},
    {"close", {35.0f, 30.0f, 0.35f}},
    {"near", {35.0f, 30.0f, 0.18f}},
}};

// Everything that decides the frame on screen. The window fills it from what it shows, not from
// what the user just asked for, so a command written from it reproduces the frame on screen.
struct WindowState {
    std::filesystem::path model; // empty: the box field
    std::string encoding;        // empty: the box field
    std::uint32_t brickSize = 0; // 0: the box field
    std::string tracer;
    std::uint32_t rayOrder = 0; // THESIS_RAY_ORDER_*
    core::OrbitCamera camera;
    // The --camera triple (yaw, pitch, factor) the camera stands at, if it has not moved since.
    std::optional<std::array<float, 3>> cameraPreset;
    resolve::ShadingParams shading;
    std::optional<std::pair<std::uint32_t, std::uint32_t>> inspectedPixel; // (x, y) in the frame
};

// The options that make thesis_renderer run the window's frame in `form`. `start` gives what the
// window does not change: resolution, box field size and seed, shader directory, output directory.
//   Screenshot: the exact camera, the view and its shading, the inspected ray.
//   Benchmark:  the orbit of the camera preset, or a fixed view at the exact camera once the
//               camera has moved away from it. Views do not change a benchmark.
[[nodiscard]] Options optionsFor(const Options& start, const WindowState& state, CommandForm form);

} // namespace thesis::app
