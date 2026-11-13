#pragma once

#include "thesis/core/Camera.h"
#include "thesis/gpu/GpuTimer.h"
#include "thesis/resolve/HitShading.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace thesis::app {

// Facts the panel shows that do not change while the window is open.
struct PanelFacts {
    struct Tracer {
        std::string name;
        std::uint64_t memoryBytes = 0;
        double buildMilliseconds = 0.0;
    };
    std::vector<Tracer> tracers;
    std::string sceneName;
    std::uint32_t primitiveCount = 0;
    std::uint64_t sceneBytes = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t rayAndHitBytes = 0;
};

// What the user controls in the window, and the panel that shows it:
// mouse orbits and zooms the camera, Tab switches tracer, 1-4 switch the view.
// Uses ImGui, so call handleInput() and draw() between Gui::newFrame() and Gui::render().
class ControlPanel {
public:
    ControlPanel(PanelFacts facts, const core::OrbitCamera& initialCamera, std::size_t initialTracer);

    void handleInput();
    void recordTimings(const std::vector<gpu::StageTiming>& timings);
    void draw();

    [[nodiscard]] std::size_t activeTracer() const { return activeTracer_; }
    [[nodiscard]] const core::OrbitCamera& camera() const { return camera_; }
    [[nodiscard]] const resolve::ShadingParams& shading() const { return shading_; }

private:
    struct Smoothed {
        double value = 0.0;
        void add(double sample) { value = value == 0.0 ? sample : 0.9 * value + 0.1 * sample; }
    };

    PanelFacts facts_;
    core::OrbitCamera initialCamera_;
    core::OrbitCamera camera_;
    resolve::ShadingParams shading_;
    std::size_t activeTracer_;
    Smoothed rayGenerationMs_;
    Smoothed traceMs_;
    Smoothed shadingMs_;
};

} // namespace thesis::app
