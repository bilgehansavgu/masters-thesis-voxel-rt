#pragma once

#include "thesis/core/Camera.h"
#include "thesis/gpu/GpuTimer.h"
#include "thesis/resolve/HitShading.h"
#include "thesis/tracing/RayLog.h"

#include "RayLogText.h"
#include "RayWorkSummary.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace thesis::app {

// Facts the panel shows. They change only when the scene is rebuilt (another encoding or
// another brick size).
struct PanelFacts {
    struct Tracer {
        std::string name;
        std::string description; // one line, from the tracer registry
        std::uint64_t memoryBytes = 0;
        double buildMilliseconds = 0.0;
        bool buildOnCpu = false;
        bool countsWork = false;
        bool findsHits = true; // false: a baseline (the null tracer); its time is the floor
    };
    struct Encoding {
        std::string name;
        std::string description; // one line, from the encoding registry
    };
    struct SceneBuffer {
        std::string name;
        std::uint64_t bytes = 0;
    };
    std::vector<Tracer> tracers;
    std::vector<std::string> skippedTracers; // "name: reason", for tracers that cannot handle the scene
    std::vector<Encoding> encodings;         // empty: the scene has no encodings to choose from
    std::size_t encoding = 0;                // the encoding of this scene, an index into `encodings`
    std::vector<std::uint32_t> brickSizes;   // empty: the scene has no bricks
    std::uint32_t brickSize = 0;             // the brick size of this scene
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

// What the user controls in the window, and the panel that shows it.
//
//   Tracers    a table with one row for each tracer. A click on a row, or Tab / Shift+Tab,
//              switches the tracer. "Measure all" runs every tracer for some frames at the
//              camera of that moment and fills the time column, so the rows can be compared.
//              The driver runs a new shader slower for its first seconds (see Benchmark.cpp),
//              so the panel keeps track of how long each shader has run. "Measure all" lets a
//              new shader run that long first, and the table marks a time from a new shader.
//   Encodings  the same kind of list for the encodings, and the brick size. A change here
//              rebuilds the scene: the caller does that when wantedEncoding() or
//              wantedBrickSize() differ from the scene it has, and then calls setFacts().
//   Views      what the image shows: the hit, or the work of each ray. With a work view on, the
//              panel also shows the average work of a ray in the frame on screen.
//   Inspector  a right click on the image picks one ray. The panel lists every step that this
//              ray took: the cells or nodes of the search structure, the primitives, and inside
//              each brick the mipmap squares, the cells and the runs (the ray log,
//              thesis/shared/RayLog.h). Esc lets the ray go.
//
// The times in the panel are live numbers of the window. They are for looking, not for the
// thesis: numbers for the thesis come from --benchmark (Benchmark.cpp), which has a warm-up,
// repeats and a results file.
//
// Uses ImGui, so call handleInput() and draw() between Gui::newFrame() and Gui::render().
class ControlPanel {
public:
    ControlPanel(PanelFacts facts, const core::OrbitCamera& initialCamera, std::size_t initialTracer);

    void handleInput();
    // The GPU times of the frame that was drawn last.
    void recordTimings(const std::vector<gpu::StageTiming>& timings);
    void draw();

    // New facts after the scene was rebuilt. The active tracer is kept if it still exists.
    void setFacts(PanelFacts facts);
    // The scene that the user asked for could not be built. The panel shows `message` until the
    // user makes the next choice. Call setFacts() with the scene that is shown instead.
    void reportProblem(std::string message);

    // What the keys and the clicks do.
    void selectNextTracer(int step = 1);
    void selectNextEncoding(int step = 1);
    void selectNextBrickSize(int step = 1);
    void selectView(resolve::ShadingMode mode) { shading_.mode = mode; }
    void measureAll();
    void stopMeasuring();
    // The ray inspector: the ray of pixel (x, y) of the frame, or none.
    void inspectPixel(std::uint32_t x, std::uint32_t y);
    void stopInspecting();
    [[nodiscard]] bool measuring() const { return measurement_.has_value(); }

    [[nodiscard]] std::size_t activeTracer() const { return activeTracer_; }
    // The encoding and the brick size that the user wants.
    [[nodiscard]] std::size_t wantedEncoding() const { return wantedEncoding_; }
    [[nodiscard]] std::uint32_t wantedBrickSize() const { return wantedBrickSize_; }
    // True when this frame must run the counting variant of the tracer: a work view is on, or
    // the ray inspector looks at a ray.
    [[nodiscard]] bool countWork() const
    {
        return (resolve::showsWork(shading_.mode) || inspectedPixel_.has_value()) && !measuring();
    }
    // The ray whose steps the ray log of this frame must hold.
    [[nodiscard]] std::optional<std::uint32_t> loggedRay() const;
    // The ray log of the frame that was drawn last (from FramePipeline::rayLog()).
    void recordRayLog(std::optional<tracing::RayLog> log);
    // True when the panel wants new averages of the work of the rays (about once a second while a
    // work view is on). The caller then reads the work of the last frame and calls recordWorkSummary().
    [[nodiscard]] bool wantsWorkSummary() const;
    void recordWorkSummary(const RayWorkSummary& summary);
    [[nodiscard]] const core::OrbitCamera& camera() const { return camera_; }
    [[nodiscard]] const resolve::ShadingParams& shading() const { return shading_; }

private:
    struct Smoothed {
        double value = 0.0;
        void add(double sample) { value = value == 0.0 ? sample : 0.9 * value + 0.1 * sample; }
    };
    // "Measure all" while it runs: the tracer that is measured now, and its samples so far.
    struct Measurement {
        std::size_t tracer = 0;
        std::size_t returnTo = 0; // the tracer that was active before
        std::uint32_t skipped = 0;
        std::vector<double> samples;
    };
    // The trace time of one tracer, and the camera position that it belongs to.
    struct TraceTime {
        double milliseconds = 0.0;
        std::uint64_t cameraVersion = 0; // 0: not measured yet
        bool fromNewShader = false;      // the shader had not run long enough: the time is too high
    };
    // The size of an encoding, known once the encoding was shown with the current brick size.
    struct EncodingSize {
        std::uint64_t bytes = 0;
        double bitsPerVoxel = 0.0;
    };
    // The frame that is on its way through the GPU: recordTimings() gets its times next.
    struct FrameInFlight {
        std::size_t tracer = 0;
        std::uint64_t cameraVersion = 0;
        bool countedWork = false;
    };

    void cameraMoved() { ++cameraVersion_; }
    // Seconds that the shader of tracer `tracer` has run in this program, for the encoding on screen.
    [[nodiscard]] double& shaderSeconds(std::size_t tracer);
    [[nodiscard]] bool shaderIsNew(std::size_t tracer);
    void rememberEncodingSize();
    [[nodiscard]] std::optional<double> floorMilliseconds() const;
    void drawTracers();
    void drawEncodings();
    void drawViews();
    void drawWorkSummary();
    void drawInspector();
    void drawInspectedPixelMark() const;
    void drawTimes();
    void drawScene();

    PanelFacts facts_;
    core::OrbitCamera initialCamera_;
    core::OrbitCamera camera_;
    std::uint64_t cameraVersion_ = 1; // goes up each time the camera moves
    resolve::ShadingParams shading_;
    std::size_t activeTracer_;
    std::size_t wantedEncoding_;
    std::uint32_t wantedBrickSize_;
    std::string problem_;

    std::vector<TraceTime> traceTimes_;                      // one for each tracer of facts_
    std::vector<std::optional<EncodingSize>> encodingSizes_; // one for each encoding of facts_
    std::uint32_t encodingSizesBrickSize_ = 0;               // the brick size that encodingSizes_ belong to
    // Seconds of use for each shader, by "<tracer>/<encoding>". A shader is the same program
    // for every brick size, and the driver keeps it when the scene is built again.
    std::map<std::string, double> shaderSeconds_;
    std::optional<Measurement> measurement_;
    std::optional<FrameInFlight> frameInFlight_;
    std::optional<std::pair<std::uint32_t, std::uint32_t>> inspectedPixel_; // (x, y) in the frame
    std::optional<tracing::RayLog> rayLog_;
    std::vector<RayLogLine> rayLogLines_;
    core::RayWork rayLogWork_{}; // the work counters of the logged ray, counted from its log
    bool onlyImportantSteps_ = false;
    std::optional<RayWorkSummary> workSummary_;
    std::chrono::steady_clock::time_point workSummaryTime_{};
    Smoothed rayGenerationMs_;
    Smoothed traceMs_; // of the frames on screen, whatever shader variant they ran
    Smoothed shadingMs_;
};

} // namespace thesis::app
