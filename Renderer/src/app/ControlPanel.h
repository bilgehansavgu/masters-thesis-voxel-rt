#pragma once

// Side: View

#include "thesis/core/Camera.h"
#include "thesis/gpu/StageTiming.h"
#include "thesis/resolve/ShadingParams.h"
#include "thesis/tracing/RayLog.h"

#include "thesis/encoding/Encoding.h"
#include "thesis/metrics/RayWorkSummary.h"
#include "thesis/voxel/MeshImport.h"
#include "thesis/voxel/VoxelFile.h"

#include <imgui.h>

#include "CameraBookmarks.h"
#include "Letterbox.h"
#include "RayLogText.h"
#include "RecentFiles.h"
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
#include <utility>
#include <vector>

namespace thesis::app {

using metrics::RayWorkSummary;
using metrics::WorkCounter;

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
        bool countsCycles = false;      // tracing::ITracer::countsCycles
        DiagramFactory diagram;         // the search structure for the Explore tab; empty or null: none
        StructureStepText describeStep; // a ray log step through the search structure in words
        bool warpsFollowRays = true;    // ray i runs on thread i (tracing::ITracer::warpsFollowRayList)
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
    // The scenes to choose from: every voxel file of the model folder and the files opened since
    // the start, then the box field (an empty path).
    std::vector<std::filesystem::path> models;
    // The header of each voxel file of `models`, same index; none for the box field and for a
    // file whose header cannot be read. "Import mesh" estimates its result from these.
    std::vector<std::optional<voxel::VoxelFileSummary>> modelSummaries;
    std::size_t model = 0;                 // the scene on screen, an index into `models`
    double loadMilliseconds = 0.0;         // reading the voxel file of the model on screen
    std::filesystem::path importDirectory; // where "Import mesh" writes its voxel files
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
    std::uint32_t warpSize = 32; // threads the GPU runs together (gpu::DeviceInfo::subgroupSize)
};

// "Save image": where the frame goes, and the command that renders it again (into a .txt file
// next to it). With `second`, the A/B comparison is on and its right image goes there too.
struct SaveImageRequest {
    std::filesystem::path file;
    std::optional<std::filesystem::path> second;
    std::string command;

    // The file the command goes to: the image's name with .txt.
    [[nodiscard]] std::filesystem::path commandFile() const
    {
        std::filesystem::path text = file;
        text.replace_extension(".txt");
        return text;
    }
    // Every file the save writes: the image, the command and, with A/B on, the B image.
    [[nodiscard]] std::vector<std::filesystem::path> files() const
    {
        std::vector<std::filesystem::path> result{file, commandFile()};
        if (second)
            result.push_back(*second);
        return result;
    }
};

// The windows around the image that can fold away, to give the image the room.
enum class PanelWindow : std::size_t { Settings, Inspector, Measurements };
inline constexpr std::size_t kPanelWindows = 3;

// "Import mesh" in Settings, Scene: a triangle mesh (PLY, OBJ, glTF) to voxelize into the model folder
// (PanelFacts::importDirectory) and open.
struct MeshImportRequest {
    std::filesystem::path mesh;
    voxel::VoxelizeOptions voxelize;
    voxel::UpAxis upAxis = voxel::UpAxis::Y;
};

// The right-click menu of the model list: a voxel file leaves the list, and maybe the disk.
struct ModelListRequest {
    enum class Action {
        Remove,  // out of the list; the file stays (a file of the model folder is back at the next start)
        Recycle, // out of the list and into the Recycle Bin; the user confirmed it
    };
    Action action = Action::Remove;
    std::filesystem::path file;
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
    // New facts of the same scene, built again (after Compare view freed it on the GPU). The
    // tracers are new objects, so their hooks (diagram, describeStep) must come from `facts`; the
    // inspected ray, the times and the views stay. Falls back to setFacts() if the tracers differ.
    void sceneRebuilt(PanelFacts facts);
    // A new start camera, after another model was opened. The camera goes there.
    void resetCamera(const core::OrbitCamera& initialCamera, std::optional<std::array<float, 3>> initialPreset);
    // While the caller builds a scene: what it does, for the status bar ("Loading lucy-8192.voxels").
    // Empty: nothing is being built. While it is set, the choices of the scene wait.
    void setLoading(std::string what)
    {
        loading_ = std::move(what);
        if (loading_.empty())
            loadingProgress_ = {};
    }
    // While the caller builds a scene: the share done (none: unknown) and whether it can stop now.
    void setLoadingProgress(std::optional<float> done, bool cancellable) { loadingProgress_ = {done, cancellable}; }
    // True once after the user pressed Cancel on the work that setLoading() names.
    [[nodiscard]] bool takeCancelRequest() { return std::exchange(cancelRequest_, false); }
    // The list of recently opened files that the panel offers and adds to. Without this call the
    // lists live only while the program runs.
    void useRecentFiles(RecentFiles recent) { recent_ = std::move(recent); }
    // The camera bookmarks that the panel offers and changes. Without this call they are the
    // defaults and live only while the program runs. A storage file that could not be read shows
    // as a problem.
    void useCameraBookmarks(CameraBookmarks bookmarks)
    {
        if (!bookmarks.problem().empty())
            problem_ = bookmarks.problem();
        bookmarks_ = std::move(bookmarks);
    }
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
    // Asks the caller to voxelize a mesh and open the voxel file, as "Import mesh" does. Ignored
    // while a scene is built or a measurement runs.
    void requestImport(MeshImportRequest request);
    // The import the user asked for, once. The caller runs it like a scene change (setLoading,
    // then setFacts or reportProblem).
    std::optional<MeshImportRequest> takeImportRequest() { return std::exchange(importRequest_, std::nullopt); }
    // Asks the caller to open a voxel file from any folder; it joins the model list. Ignored while
    // a scene is built or a measurement runs.
    void requestOpen(std::filesystem::path voxelFile);
    // The voxel file the user asked to open, once. The caller runs it like a scene change.
    std::optional<std::filesystem::path> takeOpenRequest() { return std::exchange(openRequest_, std::nullopt); }
    // Asks the caller to take a voxel file out of the model list (and to recycle it, for
    // Action::Recycle). Ignored for the model on screen, and while a scene is built or a
    // measurement runs. The file also leaves the list of recent files.
    void requestModelListChange(ModelListRequest request);
    std::optional<ModelListRequest> takeModelListRequest() { return std::exchange(modelListRequest_, std::nullopt); }
    // The caller changed the model list without building another scene: the new list, the header
    // of each file, and the index of the model on screen in it.
    void setModelList(std::vector<std::filesystem::path> models,
                      std::vector<std::optional<voxel::VoxelFileSummary>> summaries, std::size_t model);
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
    // Moves the camera to bookmark `index` of the camera bookmarks.
    void selectBookmark(std::size_t index);
    // Keeps the current camera as a bookmark named `name` (an empty name: "view N"). A bookmark of
    // that name is replaced.
    void saveBookmark(std::string name);
    void removeBookmark(std::size_t index);
    [[nodiscard]] const CameraBookmarks& bookmarks() const { return bookmarks_; }
    // Puts the command that reproduces the frame on screen on the clipboard.
    void copyCommand(CommandForm form);
    void selectView(resolve::ShadingMode mode);
    // Count views: each pixel shows the largest count of its warp. Ignored for a tracer whose
    // warps do not follow the ray list.
    void showWarpMaximum(bool on);
    // The ranges of the views follow the frame: the count at the end of the colour map is the
    // 99th percentile of the rays with work, the distance range spans the visible hits. Off: the
    // ranges stay as they are, so the colours of two tracers compare.
    void setAutoRanges(bool on);
    // The A/B comparison: the image shows the active tracer left of a line and tracer `index`
    // right of it, in the same view. None: off.
    void compareInImage(std::optional<std::size_t> index);
    // The tracer of the right part of the image, while the comparison can show the view; else none.
    [[nodiscard]] std::optional<std::size_t> splitTracer() const;
    [[nodiscard]] float splitAt() const { return splitAt_; }
    // The shading of `tracer`'s frame: the view of the panel, without the warp maximum unless
    // every tracer in the image (both sides of the A/B image) has warps that follow the ray list.
    [[nodiscard]] resolve::ShadingParams shadingFor(std::size_t tracer) const;
    // True if every tracer in the image runs ray i on thread i, so the image can show warps.
    [[nodiscard]] bool imageWarps() const;
    // True when the frame of `tracer` must run its counting variant (a work view is on).
    [[nodiscard]] bool countWorkFor(std::size_t tracer) const;
    // True if the active tracer runs ray i on thread i, so its rays group into warps.
    [[nodiscard]] bool activeTracerWarps() const;
    // "Save image": the frame on screen goes to `file`; none: into the folder of the last image,
    // with a name made of the scene, the tracer and the view.
    void saveImage(std::optional<std::filesystem::path> file = std::nullopt);
    // The image the user asked to save, once. The caller writes it and calls imageSaved().
    std::optional<SaveImageRequest> takeSaveImageRequest() { return std::exchange(saveRequest_, std::nullopt); }
    void imageSaved(const std::filesystem::path& file);
    // Folds a window away (the image gets its room) or opens it again.
    void fold(PanelWindow window, bool folded);
    [[nodiscard]] bool folded(PanelWindow window) const { return folded_.at(static_cast<std::size_t>(window)); }
    // Folds every window away, or opens them all if all are folded (key F).
    void toggleImageOnly();
    // Opens the ray inspector on the ray with the largest count of `counter` in the frame.
    // False if there is no work summary of the camera of now, so no ray to open.
    bool inspectCostliest(WorkCounter counter);
    // True if the work summary belongs to the camera of now, so its costliest ray can be opened.
    [[nodiscard]] bool costliestIsCurrent() const;
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
        return !loading_.empty() || importRequest_ || openRequest_ || modelListRequest_ ||
               wantedModel_ != facts_.model || wantedEncoding_ != facts_.encoding ||
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
    // The shading of the active tracer's frame.
    [[nodiscard]] resolve::ShadingParams shading() const { return shadingFor(activeTracer_); }
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
    // True if the depth belongs to the camera of now (for --self-test).
    [[nodiscard]] bool depthIsCurrent() const { return depthVersion_ == cameraVersion_ && !depth_.distance.empty(); }
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
        bool split = false; // a second tracer ran in the same frame: its time is not one tracer's
    };

    // Every move of the camera goes through here: it is no longer at a preset.
    void cameraMoved()
    {
        ++cameraVersion_;
        cameraPreset_.reset();
        bookmark_.reset();
    }
    void returnCamera(); // back to the start camera (R)
    // Adds a file to a list of recent files; a list that cannot be saved shows as a problem.
    void rememberFile(const std::string& kind, const std::filesystem::path& file);
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
    // The right-click menu of one file of the model list; `listed`: in the model list, not only recent.
    void drawModelMenu(const std::filesystem::path& file, bool listed, bool locked);
    void drawRecycleConfirm(); // the "Move to Recycle Bin?" question of drawModelMenu
    void drawModelFacts();     // inside Scene: grid, voxels, bricks, fill, file size, load time
    void drawImportMesh();     // inside Scene: mesh file, resolution, up axis, rule, estimate, Import button
    void drawImportResolution();
    void drawImportEstimate(const std::filesystem::path& mesh);
    void drawLoadingProgress(); // progress bar and Cancel of the work on the worker thread
    void drawEncodingSettings();
    void drawRaySettings();
    void drawSearchSettings();
    void drawOutputSettings();
    void drawLight();       // light sliders for the lit views
    void drawLightSphere(); // the light on a sphere seen from the camera; click to place the light
    void drawKeys();
    void drawBookmarks();          // inside Rays: the camera bookmarks
    void drawComparisonChoice();   // inside Search: the tracer of the A/B comparison
    void drawImageToolbar();       // on the image: fold the windows, save the image
    void drawSplitOverlay() const; // on the image: the A/B line and the names of both tracers
    void drawHistogram(WorkCounter counter);
    // False for a counter that the active tracer does not count (structure steps, cycles).
    [[nodiscard]] bool activeTracerCounts(WorkCounter counter) const;
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

    // A view that cannot be shown for tracer `tracer`, with the reason; nullptr if it can.
    [[nodiscard]] const char* unavailableViewFor(resolve::ShadingMode mode, std::size_t tracer) const;
    // The counter that a count view shows; none for the other views.
    [[nodiscard]] static std::optional<WorkCounter> counterOf(resolve::ShadingMode mode);
    // Remembers the view's colour scale and takes the one of `mode` (each count view keeps its own).
    void switchScale(resolve::ShadingMode mode);
    void tellUser(std::string message); // one line in the status bar for a few seconds

    CameraBookmarks bookmarks_;
    std::optional<std::string> bookmark_; // the bookmark the camera stands at, until it moves
    std::array<char, 64> bookmarkName_{};
    std::optional<std::size_t> renaming_; // the bookmark whose name is being edited
    std::array<char, 64> renameText_{};
    bool renameFocus_ = false; // the name field gets the keyboard in its first frame
    bool autoRanges_ = true;
    std::map<resolve::ShadingMode, resolve::ColorScale> scales_; // the scale of each count view
    std::optional<std::size_t> splitTracer_;
    float splitAt_ = 0.5f;
    bool draggingSplit_ = false;
    std::optional<SaveImageRequest> saveRequest_;
    std::filesystem::path imageFolder_; // where the last image went
    std::array<bool, kPanelWindows> folded_{};
    std::array<bool, kPanelWindows> layoutFolded_{}; // what the dock layout was built for
    WorkCounter histogramCounter_ = WorkCounter::Cycles;
    std::string notice_;
    std::chrono::steady_clock::time_point noticeTime_{};
    bool pickAfterReload_ = false;
    int compareChoice_ = 0;
    std::optional<std::string> compareRequest_;
    // "Import mesh": the text field, the choices, and the request the caller has not taken yet.
    std::array<char, 1024> importPath_{};
    MeshImportRequest importChoice_;
    bool customResolution_ = false; // the resolution is typed, not picked from the list
    std::optional<MeshImportRequest> importRequest_;
    std::optional<std::filesystem::path> openRequest_;
    std::optional<ModelListRequest> modelListRequest_;
    std::optional<std::filesystem::path> recycleCandidate_; // asked about in drawRecycleConfirm()
    bool openRecycleConfirm_ = false;
    RecentFiles recent_;
    struct LoadingProgress {
        std::optional<float> done;
        bool cancellable = false;
    } loadingProgress_;
    bool cancelRequest_ = false;
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
    // The frame that recordTimings() last took: its hits and work are in the buffers until the
    // next frame is recorded. wantsDepth() and wantsWorkSummary() ask about it.
    std::optional<FrameInFlight> finishedFrame_;
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
    std::uint64_t workSummaryCameraVersion_ = 0; // the camera of the frame the summary was taken from
    Smoothed rayGenerationMs_;
    Smoothed traceMs_; // of the frames on screen, whatever shader variant they ran
    Smoothed shadingMs_;
};

} // namespace thesis::app
