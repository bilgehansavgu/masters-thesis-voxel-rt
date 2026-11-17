#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace thesis::app {

struct Options {
    // What to run
    bool benchmark = false;
    std::filesystem::path screenshot; // not empty: write one frame to this PNG file, no window
    bool listTracers = false;
    bool listEncodings = false;
    std::vector<std::string> tracers; // benchmark: which to run (empty = all); interactive: first one shown

    // Scene: a voxel model under one of the encodings, or else the box field of step 0
    std::filesystem::path model; // a .voxels file written by thesis_model; empty = box field
    std::string encoding;        // the encoding to start with; empty = the first one
    std::uint32_t brickSize = 0; // voxels along one side of a brick: 8, 16 or 32; 0 = as in the voxel file
    std::uint32_t gridSize = 20; // box field: boxes per side
    std::uint32_t seed = 1;      // box field: random seed

    // Render resolution: the ray count. Independent of the window size.
    std::uint32_t width = 1920;
    std::uint32_t height = 1080;

    // The camera at the start: yaw and pitch in degrees, then the distance as a factor of the
    // distance that shows the whole scene. Benchmark: where the orbit starts and how far out it is.
    std::vector<float> camera{35.0f, 30.0f, 1.0f};
    std::uint32_t view = 1; // screenshot: the view, numbered as in the panel of the window (1 to 12)
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
    bool validation = false;
    std::uint32_t exitAfterFrames = 0; // interactive: close after this many frames (smoke tests)
    bool selfTest = false;             // interactive: use every control of the panel once, then close
    std::filesystem::path shaderDirectory;
};

} // namespace thesis::app
