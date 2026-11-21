#pragma once

// Side: CPU

#include "thesis/core/Camera.h"
#include "thesis/core/HitComparison.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace thesis::app {

// The result of "Compare this view": both runs at one camera pose (compareView in Benchmark.h).
struct ViewMeasurement {
    std::string tracer, encoding;
    std::filesystem::path file;
    double medianMs, spreadPercent, cellsPerRay, skipsPerRay;
    bool settled;
    std::uint64_t sceneBytes, structureBytes;
};
struct ViewComparison {
    core::OrbitCamera camera;
    std::vector<ViewMeasurement> rows;
    // The second run's hits against the first run's, with the rule of the agreement test:
    // a last-bit difference in the distance is not a disagreement.
    core::HitComparison hits;
};

} // namespace thesis::app
