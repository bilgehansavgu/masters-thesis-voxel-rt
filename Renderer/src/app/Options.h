#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace thesis::app {

struct Options {
    // What to run
    bool benchmark = false;
    bool listTracers = false;
    bool listEncodings = false;
    std::vector<std::string> tracers; // benchmark: which to run (empty = all); interactive: first one shown

    // Scene: a voxel model under one of the encodings, or else the box field of step 0
    std::filesystem::path model; // a .voxels file written by thesis_model; empty = box field
    std::string encoding;        // the encoding to start with; empty = the first one
    std::uint32_t gridSize = 20; // box field: boxes per side
    std::uint32_t seed = 1;      // box field: random seed

    // Render resolution: the ray count. Independent of the window size.
    std::uint32_t width = 1920;
    std::uint32_t height = 1080;

    // Benchmark protocol
    double warmupSeconds = 3.0;     // minimum; continues until the GPU clock has settled
    double warmupMaxSeconds = 20.0; // give up waiting for a settled clock after this
    std::uint32_t frames = 240;     // one full orbit around the scene per repeat
    std::uint32_t repeats = 5;
    double cooldownSeconds = 0.0; // idle time between tracers, lets the GPU cool down
    std::filesystem::path outputDirectory = "results";

    // Infrastructure
    bool validation = false;
    std::uint32_t exitAfterFrames = 0;     // interactive: close after this many frames (smoke tests)
    std::uint32_t switchEncodingEvery = 0; // interactive: next encoding every N frames (smoke tests)
    std::filesystem::path shaderDirectory;
};

} // namespace thesis::app
