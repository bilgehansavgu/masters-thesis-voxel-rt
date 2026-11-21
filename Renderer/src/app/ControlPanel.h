#pragma once

// Side: View

#include "thesis/core/Camera.h"
#include "thesis/gpu/StageTiming.h"
#include "thesis/resolve/ShadingParams.h"
#include "thesis/tracing/RayLog.h"

#include "thesis/encoding/Encoding.h"

#include <imgui.h>

#include "Letterbox.h"
#include "RayLogText.h"
#include "RayWorkSummary.h"
#include "StructureView.h"
#include "ViewComparison.h"
#include "WindowCommand.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
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
        std::string description;   // one line, from the tracer registry
        std::string label;         // a few words for people (tracing::TracerText)
        std::string method;        // how the search works (tracing::TracerText)
        std::string structureStep; // what structureSteps counts (tracing::TracerText)
        std::uint64_t memoryBytes = 0;
        double buildMilliseconds = 0.0;
        bool buildOnCpu = false;
        bool countsWork = false;
        bool findsHits = true; // false: a baseline (the null tracer); its time is the floor
        bool countsStructureSteps = false;
        DiagramFactory diagram;         // the search structure for the Explore tab; empty or null: none
        StructureStepText describeStep; // a ray log step through the search structure in words
    };
    struct Encoding {
        std::string name;
        std::string description;      // one line, from the encoding registry
        bool skipsEmptySpace = false; // the brick test skips empty space (min/max mipmap)
        std::string skippingTwin;     // the same bricks with skipping the other way; empty: none
    };
    struct SceneBuffer {
        std::string name;
        std::uint64_t bytes = 0;
    };
    // The scenes to choose from: every voxel file of the model folder, then the box field (an empty path).
    std::vector<std::filesystem::path> models;
    std::size_t model = 0; // the scene on screen, an index into `models`
    std::vector<Tracer> tracers;
    std::vector<std::string> skippedTracers;       // "name: reason", for tracers that cannot handle the scene
    std::vector<Encoding> encodings;               // empty: the scene has no encodings to choose from
    std::size_t encoding = 0;                      // the encoding of this scene, an index into `encodings`
    encoding::EncodingChoices encodingChoices{{}}; // the settings of each encoding, and the switches
    std::vector<std::uint32_t> brickSizes;         // empty: the scene has no bricks
    std::uint32_t brickSize = 0;                   // the brick size of this scene
    std::string sceneName;
    std::string sceneDetails;
    LessonGeometry geometry;
    core::Bounds sceneBounds;
    // Writes the thesis_renderer command that reproduces a window state (WindowCommand.h).
    std::function<std::string(const WindowState&, CommandForm)> command;
    std::uint32_t primitiveCount = 0;
    std::uint64_t voxelCount = 0; // 0: the scene has no voxels
    std::vector<SceneBuffer> sceneBuffers;
    std::uint64_t sceneBytes = 0;
    double encodeMilliseconds = 0.0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t rayOrder = 0; // where the ray of a pixel is in the ray list: THESIS_RAY_ORDER_*
    std::uint64_t rayAndHitBytes = 0;
};

// What the user controls in the window, and the windows that show it. A dock layout places them
// around the image (one file each):
//
//   Settings      left. The settings in the order a ray meets them: Scene (model), Encoding
//                 (layout, mipmap, brick size), Rays (ray order, camera), Search (tracer), Output
//                 (view, colour scale, light). A change of model, encoding, brick size or ray
//                 order rebuilds the scene: the caller does that when wanted*() differ from the
//                 scene it has, and then calls setFacts().
//   Inspector     right. A right click on the image (without moving the mouse) picks one ray:
//                 its walk in stages (RayLesson), a cut through the search structure of the
//                 tracer (StructureView), and every step of the ray log (thesis/shared/RayLog.h).
//   Measurements  under the image. Quick timing runs every tracer for some frames at the
//                 camera of that moment. The driver runs a new shader slower for its first
//                 seconds (see Benchmark.cpp), so the panel keeps track of how long each shader
//                 has run, lets a new shader run that long first, and marks a time from a new
//                 shader. Also: the average work of a ray, the comparison at a fixed view, the
//                 memory of the scene, and the command that reproduces the frame.
//   Status bar    one line at the bottom with every setting of the frame on screen.
//
// The times in the panel are live numbers of the window. They are for looking, not for the
// thesis: numbers for the thesis come from --benchmark (Benchmark.cpp), which has a warm-up,
// repeats and a results file.
//
// Uses ImGui, so call handleInput() and draw() between Gui::newFrame() and Gui::render().
class ControlPanel {
public:
    // `initialPreset`: the --camera triple that `initialCamera` stands at, if it is one.
    ControlPanel(PanelFacts facts, const core::OrbitCamera& initialCamera,
                 std::optional<std::array<float, 3>> initialPreset, const resolve::ShadingParams& initialShading,
                 std::size_t initialTracer);

    void handleInput();
    // The GPU times of the frame that was drawn last.
    void recordTimings(const std::vector<gpu::StageTiming>& timings);
    void draw();

    // New facts after the scene was rebuilt. The active tracer is kept if it still exists.
    void setFacts(PanelFacts facts);
    // A new start camera, after another model was opened. The camera goes there.
    void resetCamera(const core::OrbitCamera& initialCamera, std::optional<std::array<float, 3>> initialPreset);
    // While the caller builds a scene: what it does, for the status bar ("Loading lucy-8192.voxels").
    // Empty: nothing is being built. While it is set, the choices of the scene wait.
    void setLoading(std::string what) { loading_ = std::move(what); }
    // The frame pipeline was rebuilt for another ray order (THESIS_RAY_ORDER_*).
    void rayOrderChanged(std::uint32_t order);
    // The scene the user asked for could not be built: the choices go back to the scene on screen.
    void dropSceneChange()
    {
        wantedModel_ = facts_.model;
        wantedEncoding_ = facts_.encoding;
        wantedBrickSize_ = facts_.brickSize;
    }
    [[nodiscard]] const PanelFacts& facts() const { return facts_; }
    // The scene that the user asked for could not be built. The panel shows `message` until the
    // user makes the next choice. Call setFacts() with the scene that is shown instead.
    void reportProblem(std::string message);
    std::optional<std::string> takeComparisonRequest();
    void showComparison(ViewComparison result);
    const std::string& tracerName() const { return facts_.tracers.at(activeTracer_).name; }

    // What the keys and the clicks do.
    void selectNextTracer(int step = 1);
    void selectTracer(std::size_t index); // index into PanelFacts::tracers
    void selectNextEncoding(int step = 1);
    void selectNextBrickSize(int step = 1);
    void selectModel(std::size_t index);
    // Sets one encoding switch (EncodingChoices::Switch) to one of its values.
    void selectEncodingSetting(const std::string& setting, const std::string& value);
    void selectRayOrder(std::uint32_t order);
    // Moves the camera to kCameraPresets[index].
    void selectCameraPreset(std::size_t index);
    // Puts the command that reproduces the frame on screen on the clipboard.
    void copyCommand(CommandForm form);
    void selectView(resolve::ShadingMode mode);
    void measureAll();
    // The window is in front (focused, not minimised). Timings are taken only then: the GPU clock
    // and the scheduling of a background window differ, so its times are not comparable.
    void setWindowFocused(bool focused);
    void stopMeasuring();
    // The ray inspector: the ray of pixel (x, y) of the frame, or none.
    void inspectPixel(std::uint32_t x, std::uint32_t y);
    void stopInspecting();
    void showStructure(bool on) { structure_.show(on); }
    // The next step of the walk along the recorded ray, if there is one.
    void nextRayStep();
    void previousRayStep();
    // Moves the camera close to the brick of the hit, looking from the capture direction.
    void showBrick();
    // Turns the brick close-up to the default view of the current stage.
    void orientCloseUp();
    [[nodiscard]] bool measuring() const { return measurement_.has_value(); }
    // True while the scene the user asked for is not the one on screen yet, or is being built.
    [[nodiscard]] bool sceneChangePending() const
    {
        return !loading_.empty() || wantedModel_ != facts_.model || wantedEncoding_ != facts_.encoding ||
               wantedBrickSize_ != facts_.brickSize || wantedRayOrder_ != facts_.rayOrder;
    }

    [[nodiscard]] std::size_t activeTracer() const { return activeTracer_; }
    // The scene that the user wants: model (index into PanelFacts::models), encoding, brick size, ray order.
    [[nodiscard]] std::size_t wantedModel() const { return wantedModel_; }
    [[nodiscard]] std::size_t wantedEncoding() const { return wantedEncoding_; }
    [[nodiscard]] std::uint32_t wantedBrickSize() const { return wantedBrickSize_; }
    [[nodiscard]] std::uint32_t wantedRayOrder() const { return wantedRayOrder_; }
    // What the frame on screen is made of: the input of "Copy as command".
    [[nodiscard]] WindowState windowState() const;
    // True when this frame must run the counting variant of the tracer: a work view is on, or
    // the ray inspector looks at a ray.
    [[nodiscard]] bool countWork() const
    {
        return (resolve::showsWork(shading_.mode) || (inspectedPixel_ && !lesson_)) && !measuring();
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
    // True when the overlay wants the distance to the surface of each pixel of the frame that was
    // drawn last (its camera has not moved since). The caller reads the hits and calls recordDepth().
    [[nodiscard]] bool wantsDepth() const;
    void recordDepth(DepthMap depth);
    [[nodiscard]] const resolve::ShadingParams& shading() const { return shading_; }
    // Where the image goes: the middle of the dock layout, in window pixels (Presenter::present).
    [[nodiscard]] PixelRect imageArea() const;

    // "Quick timing": the first frames of each tracer are not used, the frames after them are.
    static constexpr std::uint32_t kMeasureSkippedFrames = 10;
    static constexpr std::size_t kMeasureFrames = 60;
    // The driver runs a shader slower for about 5 seconds after its first use (Benchmark.cpp has
    // the details). A shader that ran for less than this time is "new": its time is too high.
    static constexpr double kNewShaderSeconds = 7.0;

private:
    struct Smoothed {
        double value = 0.0;
        void add(double sample) { value = value == 0.0 ? sample : 0.9 * value + 0.1 * sample; }
    };
    // "Quick timing" while it runs: the tracer that is measured now, and its samples so far.
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

    // Every move of the camera goes through here: it is no longer at a preset.
    void cameraMoved()
    {
        ++cameraVersion_;
        cameraPreset_.reset();
    }
    void returnCamera(); // back to the start camera (R)
    // Right button held: the mouse turns the camera in place, W/A/S/D/Q/E move it, the wheel
    // sets the speed. `cameraFree`: the camera may move (no inspected pixel holds it).
    void fly(bool cameraFree);
    // Seconds that the shader of tracer `tracer` has run in this program, for the encoding on screen.
    [[nodiscard]] double& shaderSeconds(std::size_t tracer);
    [[nodiscard]] bool shaderIsNew(std::size_t tracer);
    void rememberEncodingSize();
    [[nodiscard]] std::optional<double> floorMilliseconds() const;
    void moveLight(float azimuthRadians, float elevationRadians); // turns the light, angles in radians
    // The label of a tracer for people (its name if the tracer is not in this scene).
    [[nodiscard]] const char* tracerLabel(const std::string& name) const;
    // True if the encoding of that name skips empty space inside a brick.
    [[nodiscard]] bool encodingSkips(const std::string& name) const;
    void showRayFromSide();
    void toggleSideView();
    [[nodiscard]] const char* unavailableView(resolve::ShadingMode mode) const;

    // The windows, one file each. The dock layout (ControlPanel.cpp) places them around the image.
    void drawDockLayout();
    void drawImageOverlay() const; // on the image: picked pixel, ray walk, structure cut, colour bar
    // ControlPanelSettings.cpp: the settings in the order a ray meets them.
    void drawSettings();
    void drawSceneSettings();
    void drawEncodingSettings();
    void drawRaySettings();
    void drawSearchSettings();
    void drawOutputSettings();
    void drawLight();       // light sliders for the lit views
    void drawLightSphere(); // the light on a sphere seen from the camera; click to place the light
    void drawKeys();
    // ControlPanelInspector.cpp: the picked pixel, its ray walk and its ray log.
    void drawInspector();
    void drawRayWalk();
    void drawRayLog();
    // ControlPanelMeasurements.cpp: timing, work of the rays, comparison, memory, command.
    void drawMeasurements();
    void drawTiming();
    void drawWorkSummary();
    void drawComparison();
    void drawMemory();
    void drawCommand();
    // ControlPanelStatusBar.cpp: one line under everything.
    void drawStatusBar();

    bool pickAfterReload_ = false;
    int compareChoice_ = 0;
    std::optional<std::string> compareRequest_;
    std::optional<ViewComparison> result_;
    std::uint32_t resultBrickSize_ = 0;
    bool layoutBuilt_ = false;
    // The image area (the central node of the dock layout), in ImGui coordinates.
    ImVec2 imageMin_{0.0f, 0.0f};
    ImVec2 imageMax_{0.0f, 0.0f};

    StructureView structure_;
    PanelFacts facts_;
    core::OrbitCamera initialCamera_;
    std::optional<std::array<float, 3>> initialPreset_;
    core::OrbitCamera camera_;
    std::optional<std::array<float, 3>> cameraPreset_; // the preset camera_ stands at, until it moves
    std::uint64_t cameraVersion_ = 1;                  // goes up each time the camera moves
    resolve::ShadingParams shading_;
    std::size_t activeTracer_;
    std::size_t wantedModel_;
    std::size_t wantedEncoding_;
    std::uint32_t wantedBrickSize_;
    std::uint32_t wantedRayOrder_;
    std::string loading_;
    std::string copiedCommand_; // the command last put on the clipboard, shown below the buttons
    std::string problem_;
    bool windowFocused_ = true;

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
    // The preceding tracer's complete recording for the same camera and pixel.
    std::optional<std::pair<std::size_t, core::RayWork>> comparison_;
    bool onlyImportantSteps_ = false;
    bool picking_ = false;
    bool rightButtonFlew_ = false; // the right button moved the camera since it went down: no pick
    float flySpeed_ = 1.0f;        // factor on the fly speed, set with the wheel while flying
    std::optional<core::OrbitCamera> captureCamera_;
    std::optional<core::OrbitCamera> pendingObserver_;
    std::optional<RayLesson> lesson_;
    // The distance to the surface for each pixel, for the camera version depthVersion_.
    DepthMap depth_;
    std::uint64_t depthVersion_ = 0;
    std::size_t lessonStage_ = 0; // the stage of the walk (Ray, Traversal, Brick, ...)
    std::size_t lessonEvent_ = 0; // the event within that stage
    CloseUpView closeUp_;         // the turn, zoom and pan of the brick close-up
    bool observing_ = false;
    bool lastFrameCounted_ = false;
    std::optional<RayWorkSummary> workSummary_;
    std::chrono::steady_clock::time_point workSummaryTime_{};
    Smoothed rayGenerationMs_;
    Smoothed traceMs_; // of the frames on screen, whatever shader variant they ran
    Smoothed shadingMs_;
};

} // namespace thesis::app
