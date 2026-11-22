#include "WindowCommand.h"

#include <glm/trigonometric.hpp>

namespace thesis::app {

Options optionsFor(const Options& start, const WindowState& state, CommandForm form)
{
    Options options;
    options.shaderDirectory = start.shaderDirectory;
    options.outputDirectory = start.outputDirectory;
    options.width = start.width;
    options.height = start.height;
    options.gridSize = start.gridSize;
    options.seed = start.seed;

    options.model = state.model;
    options.boxExample = state.model.empty();
    options.encoding = state.encoding;
    options.brickSize = state.brickSize;
    options.tracers = {state.tracer};
    options.rayOrder = state.rayOrder;

    if (form == CommandForm::Benchmark) {
        options.benchmark = true;
        if (state.cameraPreset) {
            const std::array<float, 3>& preset = *state.cameraPreset;
            options.camera = {preset[0], preset[1], preset[2]};
        } else {
            options.fixedView = true;
            options.exactCamera = state.camera;
        }
        return options;
    }

    options.screenshot = "frame.png";
    // The exact camera, even at a preset: the frame must match bit for bit.
    options.exactCamera = state.camera;
    const resolve::ShadingParams& shading = state.shading;
    options.view = static_cast<std::uint32_t>(shading.mode) + 1;
    if (resolve::showsCount(shading.mode)) {
        options.scaleMaximum = shading.workScale.maximum;
        options.logScale = shading.workScale.kind == resolve::ScaleKind::Log2;
        options.warpMaximum = shading.warpSize > 0;
    }
    const resolve::LightDirection defaultLight;
    if (resolve::usesLight(shading.mode) &&
        (shading.light.azimuth != defaultLight.azimuth || shading.light.elevation != defaultLight.elevation)) {
        options.light = {glm::degrees(shading.light.azimuth), glm::degrees(shading.light.elevation)};
    }
    if (shading.mode == resolve::ShadingMode::Distance) {
        options.distanceRange = {shading.distanceNear, shading.distanceFar};
    }
    if (state.inspectedPixel) {
        options.inspect = {state.inspectedPixel->first, state.inspectedPixel->second};
    }
    return options;
}

} // namespace thesis::app
