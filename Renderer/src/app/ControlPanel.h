#pragma once

#include "thesis/core/Camera.h"
#include "thesis/gpu/GpuTimer.h"
#include "thesis/resolve/HitShading.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace thesis::app {

// Facts the panel shows. They change only when the scene is rebuilt (another encoding).
struct PanelFacts {
    struct Tracer {
        std::string name;
        std::uint64_t memoryBytes = 0;
        double buildMilliseconds = 0.0;
    };
    struct SceneBuffer {
        std::string name;
        std::uint64_t bytes = 0;
    };
    std::vector<Tracer> tracers;
    std::vector<std::string> skippedTracers; // "name: reason", for tracers that cannot handle the scene
    std::vector<std::string> encodings;      // empty: the scene has no encodings to choose from
    std::string sceneName;
    std::string sceneDetails;
    std::uint32_t primitiveCount = 0;
    std::uint64_t voxelCount = 0; // 0: the scene has no voxels
    std::vector<SceneBuffer> sceneBuffers;
    std::uint64_t sceneBytes = 0;
    double encodeMilliseconds = 0.0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t rayAndHitBytes = 0;
};

// What the user controls in the window, and the panel that shows it:
// mouse orbits and zooms the camera, Tab switches tracer, E switches encoding, 1-4 switch the view.
// Uses ImGui, so call handleInput() and draw() between Gui::newFrame() and Gui::render().
class ControlPanel {
public:
    ControlPanel(PanelFacts facts, const core::OrbitCamera& initialCamera, std::size_t initialTracer,
                 std::size_t initialEncoding);

    void handleInput();
    void recordTimings(const std::vector<gpu::StageTiming>& timings);
    void draw();

    // New facts after the scene was rebuilt. The active tracer is kept if it still exists.
    void setFacts(PanelFacts facts);
    // What the E key does: asks for the next encoding in the list.
    void selectNextEncoding();

    [[nodiscard]] std::size_t activeTracer() const { return activeTracer_; }
    // The encoding the user wants. The caller rebuilds the scene when it changes.
    [[nodiscard]] std::size_t activeEncoding() const { return activeEncoding_; }
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
    std::size_t activeEncoding_;
    Smoothed rayGenerationMs_;
    Smoothed traceMs_;
    Smoothed shadingMs_;
};

} // namespace thesis::app
