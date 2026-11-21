#pragma once

// Side: GPU

#include "Options.h"

#include "thesis/resolve/ColorBarImage.h"
#include "thesis/resolve/ShadingParams.h"
#include "thesis/tracing/RayLog.h"

#include <optional>
#include <string>

namespace thesis::gpu {
class Context;
}

namespace thesis::app {

// One frame rendered without a window, as --screenshot makes it.
struct RenderedFrame {
    resolve::RgbaImage image;           // the shaded frame, without the colour bar
    resolve::ShadingParams shading;     // how it was shaded
    std::string tracer;                 // the tracer that traced it
    std::string sceneName;              // for people
    std::string encoding;               // empty for the box field
    std::optional<tracing::RayLog> log; // the steps of the --inspect ray, if one was given
    std::string logText;                // the same steps in words
};

// Renders the frame that `options` describe, with the same frame pipeline as the window and the
// benchmark. The window's check of "Copy as command" compares its own frame with this one.
[[nodiscard]] RenderedFrame renderFrame(const gpu::Context& context, const Options& options);

// Renders one frame without a window and writes it to options.screenshot as a PNG file. A count
// view gets its colour bar in a strip below the image. Returns the process exit code.
int runScreenshot(const Options& options);

} // namespace thesis::app
