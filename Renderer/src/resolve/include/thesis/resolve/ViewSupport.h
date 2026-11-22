#pragma once

// Side: CPU

#include "thesis/resolve/ShadingParams.h"

namespace thesis::resolve {

// What a view needs from the tracer and the scene, as plain facts. The window and --screenshot
// ask the same question, so a view that one refuses the other refuses too.
struct ViewSupport {
    bool countsWork = false;           // tracing::ITracer::countsWork
    bool countsStructureSteps = false; // tracing::ITracer::countsStructureSteps
    bool countsCycles = false;         // tracing::ITracer::countsCycles
    bool voxelModel = false;           // the scene is a voxel model, not the box field
    bool skipsEmptySpace = false;      // the encoding has a mipmap (encoding::IEncoding::skipsEmptySpace)
};

// Why `mode` cannot be shown with `support`, in one sentence; null if it can.
[[nodiscard]] const char* unavailableView(ShadingMode mode, const ViewSupport& support);

} // namespace thesis::resolve
