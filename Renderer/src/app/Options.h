#pragma once

// Side: CPU

#include "thesis/core/Camera.h"
#include "thesis/shared/RayOrder.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace thesis::app {

struct Options {
    // What to run
    bool benchmark = false;
    bool fixedView = false;
    bool compareSkipping = false;
    std::filesystem::path openResult;
    std::optional<core::OrbitCamera> exactCamera;
    std::filesystem::path screenshot; // not empty: write one frame to this PNG file, no window
    bool listTracers = false;
    bool listEncodings = false;
    std::vector<std::string> tracers; // benchmark: which to run (empty = all); interactive: first one shown

    // Scene: a voxel model under one of the encodings, or else the box field of step 0
    std::filesystem::path model; // a .voxels file written by thesis_model; empty = box field
    bool boxExample = false;     // interactive: explicitly start on the box field
    std::string encoding;        // the encoding to start with; empty = the first one
    std::uint32_t brickSize = 0; // voxels along one side of a brick: 8, 16 or 32; 0 = as in the voxel file
    std::uint32_t gridSize = 20; // box field: boxes per side
    std::uint32_t seed = 1;      // box field: random seed

    // Render resolution: the ray count. Independent of the window size.
    std::uint32_t width = 1920;
    std::uint32_t height = 1080;
    // Where the ray of a pixel sits in the ray list: THESIS_RAY_ORDER_ROWS or _TILES
    // (thesis/shared/RayOrder.h). It decides which pixels one warp of the GPU traces together.
    std::uint32_t rayOrder = THESIS_RAY_ORDER_ROWS;

    // The camera at the start: yaw and pitch in degrees, then the distance as a factor of the
    // distance that shows the whole scene. Benchmark: where the orbit starts and how far out it is.
    std::vector<float> camera{35.0f, 30.0f, 1.0f};
    std::uint32_t view = 1; // the view, numbered as in the panel of the window (1 to 12)
    // Screenshot of a count view (--view 6, 7, 8, 10 or 11): the count at the end of the colour map,
    // and a log2 instead of a linear scale. The image gets the colour bar below it.
    float scaleMaximum = 64.0f;
    bool logScale = false;
    // The light of the lit views (1 and 5): azimuth and elevation in degrees. Empty: the default light.
    std::vector<float> light;
    // The distance view (2): the distances shown as white and black. Empty: 0 to twice the camera distance.
    std::vector<float> distanceRange;
    // The ray inspector on pixel (x, y) of the frame. Window: it starts on that ray. Screenshot:
    // it also prints every step of that ray. Empty: off.
    std::vector<std::uint32_t> inspect;

    // Benchmark protocol
    // Minimum. It covers the first seconds of a new shader, in which the driver runs it slower.
    // The warm-up goes on until the GPU clock and the trace time have settled.
    double warmupSeconds = 10.0;
    double warmupMaxSeconds = 30.0; // give up waiting for a settled clock and trace time after this
    std::uint32_t frames = 240;     // one full orbit around the scene per repeat
    std::uint32_t repeats = 5;
    double cooldownSeconds = 0.0; // idle time between tracers, lets the GPU cool down
    std::filesystem::path outputDirectory = "results";

    // Infrastructure
    std::filesystem::path program; // this program as an absolute path, for "Copy as command"
    bool validation = false;
    std::uint32_t exitAfterFrames = 0; // interactive: close after this many frames (smoke tests)
    bool selfTest = false;             // interactive: use every control of the panel once, then close
    std::filesystem::path shaderDirectory;
};

} // namespace thesis::app
