#include "Benchmark.h"

#include "FramePipeline.h"
#include "RayWorkSummary.h"
#include "SceneSetup.h"

#include "thesis/core/GpuTypes.h"
#include "thesis/core/Statistics.h"
#include "thesis/gpu/Buffer.h"
#include "thesis/gpu/Context.h"
#include "thesis/metrics/Sinks.h"
#include "thesis/metrics/SystemInfo.h"
#include "thesis/profiling/Profiling.h"
#include "thesis/shared/Hit.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <format>
#include <fstream>
#include <optional>
#include <thread>
#include <utility>

namespace thesis::app {
namespace {

std::string fileTimestamp()
{
    const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    return std::format("{:%Y%m%d-%H%M%S}", now);
}

nlohmann::json describeDevice(const gpu::Context& context)
{
    const gpu::DeviceInfo& info = context.info();
    return {{"name", info.name},
            {"driver", info.driverName},
            {"driver_version", info.driverInfo},
            {"vendor_id", info.vendorId},
            {"device_id", info.deviceId},
            {"vulkan_api", std::format("{}.{}.{}", VK_API_VERSION_MAJOR(info.apiVersion),
                                       VK_API_VERSION_MINOR(info.apiVersion), VK_API_VERSION_PATCH(info.apiVersion))},
            {"device_local_bytes", info.deviceLocalBytes}};
}

// The benchmark camera path: one full orbit around the scene per repeat.
core::CameraFrame cameraAt(const core::OrbitCamera& base, std::uint32_t frame, std::uint32_t frameCount,
                           float aspectRatio)
{
    core::OrbitCamera camera = base;
    camera.yawDegrees += 360.0f * static_cast<float>(frame) / static_cast<float>(frameCount);
    return camera.frame(aspectRatio);
}

double stageTime(const std::vector<gpu::StageTiming>& timings, std::string_view stage)
{
    for (const gpu::StageTiming& timing : timings) {
        if (stage == timing.stage) {
            return timing.milliseconds;
        }
    }
    return 0.0;
}

// The work of the rays of one tracer, added up over kWorkViews views of the benchmark orbit:
// how many primitives, cells, mipmap cells and runs a ray was tested against. It runs the counting variant
// of the tracer's shader. That variant is slower, so this happens after the timed repeats and
// takes no part in them.
nlohmann::json measureRayWork(const gpu::Context& context, FramePipeline& pipeline, tracing::ITracer& tracer,
                              const core::OrbitCamera& baseCamera, bool fixedView)
{
    const std::uint32_t kWorkViews = fixedView ? 1u : 8u;
    const std::size_t count = pipeline.batch().count();
    std::vector<core::RayWork> work(count);
    std::vector<core::Hit> hits(count);

    RayWorkSummary summary;
    for (std::uint32_t view = 0; view < kWorkViews; ++view) {
        const core::CameraFrame camera = cameraAt(baseCamera, view, kWorkViews, pipeline.aspectRatio());
        context.submitAndWait([&](VkCommandBuffer cmd) {
            pipeline.begin(cmd);
            pipeline.trace(cmd, tracer, camera, true);
        });
        (void)pipeline.collectTimings();
        const std::vector<std::byte> workBytes = gpu::downloadBuffer(context, *pipeline.work());
        const std::vector<std::byte> hitBytes = gpu::downloadBuffer(context, pipeline.batch().hits);
        std::memcpy(work.data(), workBytes.data(), count * sizeof(core::RayWork));
        std::memcpy(hits.data(), hitBytes.data(), count * sizeof(core::Hit));
        summary.add(work, hits);
    }

    nlohmann::json result = summary.toJson();
    result["note"] = "from the counting variant of the shader, not timed";
    result["views"] = kWorkViews;
    return result;
}

// What the driver made of the timed shader: register count, instruction count and so on. The
// names come from the driver, so they are written as they are.
nlohmann::json shaderStatisticsJson(const std::vector<gpu::ShaderStatistic>& statistics)
{
    nlohmann::json result = nlohmann::json::object();
    for (const gpu::ShaderStatistic& statistic : statistics) {
        result[statistic.name] = statistic.value;
    }
    return result;
}

struct WarmupResult {
    double seconds = 0.0;
    std::uint32_t frames = 0;
    bool clockStable = false;
    std::optional<std::uint32_t> clockMhz;
    bool traceStable = false;
    std::vector<double> orbitMedians; // median trace time of each whole orbit of the warm-up, in order
};

// True when the last kStableOrbits orbits of the warm-up took the same time, within kStableSpread.
bool lastOrbitsAgree(const std::vector<double>& orbitMedians)
{
    constexpr std::size_t kStableOrbits = 3;
    constexpr double kStableSpread = 0.02;
    if (orbitMedians.size() < kStableOrbits) {
        return false;
    }
    const auto [lowest, highest] = std::minmax_element(orbitMedians.end() - kStableOrbits, orbitMedians.end());
    return *lowest > 0.0 && (*highest - *lowest) <= kStableSpread * *lowest;
}

// Renders orbits until three things hold, or until `maxSeconds`:
//
//   1. At least `minSeconds` have passed. A fixed time, not a frame count: 60 frames are seconds
//      for a slow tracer but milliseconds for a fast one. The time must cover the first seconds
//      of a shader: the driver runs a new shader slower for about 5 seconds after its first use,
//      at a steady speed, so a test of the trace time alone does not see that phase.
//   2. The GPU clock has settled (kStableReadings readings, kSamplePeriod apart, each within
//      1 % of the previous). An idle laptop GPU runs at a few hundred MHz.
//   3. The trace time has settled: the last three orbits took the same time within 2 %.
//
// The warm-up ends at the end of an orbit, so the repeats start where an orbit starts.
template <typename RenderFrame>
WarmupResult warmUp(RenderFrame&& renderFrame, std::uint32_t framesPerOrbit, double minSeconds, double maxSeconds)
{
    using Clock = std::chrono::steady_clock;
    constexpr auto kSamplePeriod = std::chrono::milliseconds(250);
    constexpr std::uint32_t kStableReadings = 4;

    const Clock::time_point start = Clock::now();
    Clock::time_point nextSample = start;
    std::optional<std::uint32_t> lastClock;
    std::uint32_t stableReadings = 0;
    std::vector<double> orbit; // trace times of the orbit that is running
    orbit.reserve(framesPerOrbit);
    WarmupResult result;

    while (true) {
        orbit.push_back(stageTime(renderFrame(result.frames % framesPerOrbit), kStageTrace));
        ++result.frames;
        const bool orbitDone = orbit.size() == framesPerOrbit;
        if (orbitDone) {
            result.orbitMedians.push_back(core::median(orbit));
            result.traceStable = lastOrbitsAgree(result.orbitMedians);
            orbit.clear();
        }

        const Clock::time_point now = Clock::now();
        if (now >= nextSample) {
            nextSample = now + kSamplePeriod;
            const std::optional<std::uint32_t> clock = metrics::currentGraphicsClockMhz();
            const bool steady = clock && lastClock &&
                                std::abs(static_cast<double>(*clock) - static_cast<double>(*lastClock)) <=
                                    0.01 * static_cast<double>(*lastClock);
            stableReadings = steady ? stableReadings + 1 : 0;
            lastClock = clock;
        }

        result.seconds = std::chrono::duration<double>(now - start).count();
        result.clockMhz = lastClock;
        result.clockStable = lastClock.has_value() && stableReadings >= kStableReadings;
        const bool clockSettled = result.clockStable || !lastClock.has_value(); // unreadable clock: not waited for
        if ((result.seconds >= minSeconds && clockSettled && result.traceStable && orbitDone) ||
            result.seconds >= maxSeconds) {
            return result;
        }
    }
}

} // namespace

struct ReferenceFrame {
    std::vector<std::byte> rays;
    std::vector<std::byte> hits;
};

static int benchmark(const Options& options, ViewComparison* comparison = nullptr, ReferenceFrame* reference = nullptr)
{
    gpu::ContextOptions contextOptions;
    contextOptions.applicationName = "Thesis Renderer (benchmark)";
    contextOptions.validation = options.validation;
    const gpu::Context context(contextOptions);
    if (options.validation) {
        std::printf("note: validation is on, timings are not representative\n");
    }

    SceneSource source(options);
    const BuiltScene built = source.build();
    const core::Scene& scene = built.scene;
    const tracing::GpuScene gpuScene = tracing::GpuScene::upload(context, scene);
    const tracing::TracerRegistry registry = makeTracerRegistry();
    const std::vector<std::string> tracerNames = selectTracers(registry, options.tracers);
    const tracing::TracerEnvironment environment{context, options.shaderDirectory};

    FramePipeline pipeline(context, options.shaderDirectory, {options.width, options.height},
                           static_cast<tracing::RayOrder>(options.rayOrder));
    const core::OrbitCamera baseCamera = startCamera(options, scene.bounds);
    if (options.fixedView) {
        const std::span<const std::byte> rays =
            reference ? std::span<const std::byte>(reference->rays) : std::span<const std::byte>();
        pipeline.freezeRays(baseCamera.frame(pipeline.aspectRatio()), rays);
    }
    const std::uint64_t rayCount = pipeline.batch().count();
    const std::string stamp = fileTimestamp();

    std::printf("GPU: %s (driver %s)\n", context.info().name.c_str(), context.info().driverInfo.c_str());
    std::printf("Scene: %s, %u primitives; %ux%u = %llu rays per frame\n", scene.name.c_str(), gpuScene.primitiveCount,
                options.width, options.height, static_cast<unsigned long long>(rayCount));
    if (!built.encoding.empty()) {
        std::printf("Encoding: %s, %.2f MiB on the GPU, %.2f bits per voxel (%s)\n", built.encoding.c_str(),
                    static_cast<double>(built.memory.totalBytes) / (1024.0 * 1024.0),
                    built.memory.bitsPerVoxel(built.voxels), built.details.c_str());
    }
    std::printf("Protocol: warm-up %.0f-%.0f s until the GPU clock and the trace time settle, then %u repeats x %u "
                "frames\n",
                options.warmupSeconds, options.warmupMaxSeconds, options.repeats, options.frames);
    std::printf("Camera: %s, yaw %.2f, pitch %.2f, distance %.4g\n", options.fixedView ? "fixed" : "orbit",
                baseCamera.yawDegrees, baseCamera.pitchDegrees, baseCamera.distance);
    if (comparison)
        comparison->camera = baseCamera;
    std::printf("%-16s %10s %10s %10s %8s %10s %10s %9s %8s\n", "tracer", "median ms", "min ms", "max ms", "spread %",
                "Mrays/s", "clock MHz", "warm-up s", "settled");

    // Medians of tracers that find hits, and the fastest baseline (null tracer), for the summary.
    std::vector<std::pair<std::string, double>> tracerMedians;
    std::optional<double> floorMs;

    for (std::size_t index = 0; index < tracerNames.size(); ++index) {
        const std::string& tracerName = tracerNames[index];
        if (index > 0 && options.cooldownSeconds > 0.0) {
            std::this_thread::sleep_for(std::chrono::duration<double>(options.cooldownSeconds));
        }

        const auto tracer = registry.create(tracerName, environment);
        if (comparison && !tracer->findsHits())
            throw std::invalid_argument("Choose one surface tracer for each comparison condition");
        tracing::PrepareReport prepared;
        try {
            prepared = tracer->prepare(gpuScene);
        } catch (const tracing::UnsupportedScene& reason) {
            if (!options.tracers.empty()) {
                throw; // the user asked for this tracer by name
            }
            std::printf("%-16s skipped: %s\n", tracerName.c_str(), reason.what());
            continue;
        }

        nlohmann::json run;
        run["schema"] = "thesis-benchmark/1";
        run["tracer"] = tracerName;
        run["build"] = metrics::buildInfo();
        run["device"] = describeDevice(context);
        run["scene"] = {{"name", scene.name},
                        {"primitive_kind", scene.primitiveKind},
                        {"primitives", gpuScene.primitiveCount},
                        {"details", built.details},
                        {"encoding", built.encoding},
                        {"brick_size", built.brickSize},
                        {"voxels", built.voxels},
                        {"encode_seconds", built.encodeSeconds},
                        {"memory", built.memory.toJson(built.voxels)},
                        {"statistics", built.statistics}};
        run["config"] = {
            {"width", options.width},
            {"height", options.height},
            {"rays_per_frame", rayCount},
            {"ray_order", options.rayOrder == THESIS_RAY_ORDER_TILES ? "tiles" : "rows"},
            {"warmup_min_seconds", options.warmupSeconds},
            {"warmup_max_seconds", options.warmupMaxSeconds},
            {"frames_per_repeat", options.frames},
            {"repeats", options.repeats},
            {"camera_path", options.fixedView ? "fixed" : "orbit, 360 degrees per repeat"},
            {"camera_start",
             {{"yaw_degrees", baseCamera.yawDegrees},
              {"pitch_degrees", baseCamera.pitchDegrees},
              {"distance_factor", baseCamera.distance / core::OrbitCamera::framing(scene.bounds).distance}}},
            {"validation", options.validation}};
        run["config"]["ray_workload"] =
            options.fixedView ? "fixed ray buffer, generated once" : "camera rays generated each frame";
        run["config"]["pose"] = {{"target", {baseCamera.target.x, baseCamera.target.y, baseCamera.target.z}},
                                 {"distance", baseCamera.distance},
                                 {"yaw", baseCamera.yawDegrees},
                                 {"pitch", baseCamera.pitchDegrees},
                                 {"fov", baseCamera.verticalFovDegrees}};
        run["scene"]["model_path"] = options.model.empty() ? "" : std::filesystem::absolute(options.model).string();
        run["scene"]["grid_size"] = options.gridSize;
        run["scene"]["seed"] = options.seed;
        run["prepare"] = {{"gpu_ms", prepared.gpuMilliseconds},
                          {"cpu_ms", prepared.cpuMilliseconds},
                          {"memory_bytes", prepared.memoryBytes},
                          {"scratch_bytes", prepared.scratchBytes},
                          {"shader_statistics", shaderStatisticsJson(prepared.shaderStatistics)}};
        run["memory_bytes"] = {{"scene", gpuScene.memoryBytes()},
                               {"tracer", tracer->memoryBytes()},
                               {"rays_and_hits", pipeline.batch().memoryBytes()},
                               {"process_device_local", context.deviceLocalUsage()}};
        const auto renderFrame = [&](std::uint32_t frame) {
            THESIS_ZONE("benchmark frame");
            const core::CameraFrame camera =
                cameraAt(baseCamera, options.fixedView ? 0u : frame, options.frames, pipeline.aspectRatio());
            context.submitAndWait([&](VkCommandBuffer cmd) {
                pipeline.begin(cmd);
                pipeline.trace(cmd, *tracer, camera);
            });
            THESIS_FRAME_MARK();
            return pipeline.collectTimings();
        };

        // Warm up first, so the recorded system state shows the clocks the measurement runs at
        // (an idle laptop GPU sits at a few hundred MHz).
        const WarmupResult warmup =
            warmUp(renderFrame, options.frames, options.warmupSeconds, options.warmupMaxSeconds);
        run["warmup"] = {{"seconds", warmup.seconds},
                         {"frames", warmup.frames},
                         {"clock_settled", warmup.clockStable},
                         {"graphics_clock_mhz", warmup.clockMhz ? nlohmann::json(*warmup.clockMhz) : nlohmann::json()},
                         {"trace_settled", warmup.traceStable},
                         {options.fixedView ? "trace_ms_median_of_each_batch" : "trace_ms_median_of_each_orbit",
                          warmup.orbitMedians}};
        run["system_after_warmup"] = metrics::captureSystemState();

        metrics::TracySink tracySink;
        const std::string encodingSuffix = built.encoding.empty() ? "" : "_" + built.encoding;
        metrics::JsonReportSink fileSink(options.outputDirectory /
                                         (stamp + "_" + tracerName + encodingSuffix + ".json"));
        metrics::MetricFanout sinks({&tracySink, &fileSink});
        sinks.beginRun(run);

        std::vector<double> allTraceMs;
        std::vector<double> repeatMedians;
        nlohmann::json repeatClocks = nlohmann::json::array();
        nlohmann::json repeatGpuStates = nlohmann::json::array();
        for (std::uint32_t repeat = 0; repeat < options.repeats; ++repeat) {
            std::vector<double> traceMs;
            for (std::uint32_t frame = 0; frame < options.frames; ++frame) {
                const std::vector<gpu::StageTiming> timings = renderFrame(frame);
                metrics::FrameSample sample{repeat, frame, rayCount, {}};
                for (const gpu::StageTiming& timing : timings) {
                    sample.stages.push_back({timing.stage, timing.milliseconds});
                }
                sinks.recordFrame(sample);
                traceMs.push_back(stageTime(timings, kStageTrace));
            }
            repeatMedians.push_back(core::median(traceMs));
            const std::optional<std::uint32_t> clock = metrics::currentGraphicsClockMhz();
            repeatClocks.push_back(clock ? nlohmann::json(*clock) : nlohmann::json());
            repeatGpuStates.push_back(metrics::captureGpuState());
            allTraceMs.insert(allTraceMs.end(), traceMs.begin(), traceMs.end());
        }

        const core::Summary frames = core::summarize(allTraceMs);
        const core::Summary repeats = core::summarize(repeatMedians);
        const double medianMs = repeats.median;
        const double megaRaysPerSecond = medianMs > 0.0 ? static_cast<double>(rayCount) / (medianMs * 1e3) : 0.0;

        const double spreadPercent = medianMs > 0.0 ? 100.0 * (repeats.max - repeats.min) / medianMs : 0.0;
        nlohmann::json summary;
        summary["trace_ms"] = {{"median_of_repeat_medians", medianMs},
                               {"repeat_medians", repeatMedians},
                               {"spread_between_repeats_percent", spreadPercent},
                               {"all_frames",
                                {{"median", frames.median},
                                 {"mean", frames.mean},
                                 {"min", frames.min},
                                 {"max", frames.max},
                                 {"stddev", frames.standardDeviation}}}};
        summary["mrays_per_second"] = megaRaysPerSecond;
        summary["graphics_clock_mhz_after_each_repeat"] = repeatClocks;
        summary["gpu_after_each_repeat"] = repeatGpuStates;
        summary["system_after"] = metrics::captureSystemState();
        if (comparison) {
            auto rays = gpu::downloadBuffer(context, pipeline.batch().rays);
            if (reference->rays.empty())
                reference->rays = std::move(rays);
            else if (reference->rays != rays)
                throw std::runtime_error("The two runs generated different camera rays. Comparison invalid.");
            const auto hits = gpu::downloadBuffer(context, pipeline.batch().hits);
            if (reference->hits.empty())
                reference->hits = hits;
            else {
                if (hits.size() != reference->hits.size())
                    throw std::runtime_error("Hit buffer sizes differ");
                const auto asHits = [rayCount](const std::vector<std::byte>& bytes) {
                    return std::span<const core::Hit>(reinterpret_cast<const core::Hit*>(bytes.data()), rayCount);
                };
                const core::HitComparison& same = comparison->hits =
                    core::compareHits(asHits(reference->hits), asHits(hits));
                summary["hit_comparison"] = {{"input_rays_verified_identical", true},
                                             {"rule", "agreement test: relative distance tolerance"},
                                             {"relative_tolerance", core::HitComparison::kRelativeTolerance},
                                             {"agrees", same.agrees()},
                                             {"rays", same.rays},
                                             {"both_miss", same.bothMiss},
                                             {"both_hit", same.bothHit},
                                             {"hit_vs_miss", same.hitVersusMiss},
                                             {"distance_mismatch", same.distanceMismatch},
                                             {"face_mismatch", same.faceMismatch},
                                             {"touching_primitive", same.primitiveMismatch},
                                             {"touching_attributes", same.attributeMismatch},
                                             {"max_relative_distance_error", same.maxRelativeDistanceError},
                                             {"reference_report", comparison->rows.front().file.filename().string()}};
            }
        }
        if (tracer->countsWork()) {
            summary["ray_work"] = measureRayWork(context, pipeline, *tracer, baseCamera, options.fixedView);
            if (!tracer->countsStructureSteps()) {
                for (const char* group : {"mean_per_ray", "mean_per_ray_with_hit", "max_per_ray"})
                    summary["ray_work"][group]["structure_steps"] = nullptr;
            }
        }
        sinks.endRun(summary);
        if (comparison) {
            const auto& mean = summary.at("ray_work").at("mean_per_ray");
            comparison->rows.push_back({tracerName, built.encoding, fileSink.path(), medianMs, spreadPercent,
                                        mean.at("cells").get<double>(), mean.at("mipmap_skips").get<double>(),
                                        warmup.traceStable && (warmup.clockStable || !warmup.clockMhz),
                                        gpuScene.memoryBytes(), tracer->memoryBytes()});
        }

        const std::string clockText = warmup.clockMhz ? std::to_string(*warmup.clockMhz) : "n/a";
        // "settled" is about the warm-up: the GPU clock and the trace time were steady when it ended.
        std::printf("%-16s %10.3f %10.3f %10.3f %8.1f %10.1f %10s %9.1f %8s\n", tracerName.c_str(), medianMs,
                    frames.min, frames.max, spreadPercent, megaRaysPerSecond, clockText.c_str(), warmup.seconds,
                    warmup.clockStable && warmup.traceStable ? "yes" : "NO");
        if (summary.contains("ray_work")) {
            const nlohmann::json& work = summary["ray_work"];
            const nlohmann::json& mean = work["mean_per_ray_with_hit"];
            std::printf("           %.1f %% of the rays hit; a ray that hits: %s structure steps, %.2f primitive "
                        "tests, %.2f mipmap cells (%.2f skipped), %.2f cells, %.2f runs\n",
                        100.0 * work["rays_with_hit"].get<double>() / work["rays"].get<double>(),
                        (mean["structure_steps"].is_null()
                             ? std::string("unavailable")
                             : std::format("{:.2f}", mean["structure_steps"].get<double>()))
                            .c_str(),
                        mean["primitive_tests"].get<double>(), mean["mipmap_cells"].get<double>(),
                        mean["mipmap_skips"].get<double>(), mean["cells"].get<double>(), mean["runs"].get<double>());
        }
        for (const gpu::ShaderStatistic& statistic : prepared.shaderStatistics) {
            if (statistic.name == "Register Count") {
                std::printf("           the timed shader uses %.0f registers per thread\n", statistic.value);
            }
        }
        std::printf("           -> %s\n", fileSink.path().string().c_str());

        if (tracer->findsHits()) {
            tracerMedians.emplace_back(tracerName, medianMs);
        } else {
            floorMs = floorMs ? std::min(*floorMs, medianMs) : medianMs;
        }
    }

    // A tracer's own search cost: its time minus what only moving rays and hits costs.
    if (floorMs && !tracerMedians.empty()) {
        std::printf("\nMemory-traffic floor (null tracer): %.3f ms. Time above it:\n", *floorMs);
        for (const auto& [name, median] : tracerMedians) {
            std::printf("  %-16s %10.3f ms\n", name.c_str(), median - *floorMs);
        }
    }

    if (options.validation && gpu::validationMessageCount() > 0) {
        std::fprintf(stderr, "\n%llu validation message(s) were reported.\n",
                     static_cast<unsigned long long>(gpu::validationMessageCount()));
        return 2;
    }
    return 0;
}

int runBenchmark(const Options& options)
{
    return benchmark(options);
}

ViewComparison compareView(Options options, const std::string& secondTracer)
{
    options.fixedView = true;
    if (options.tracers.empty())
        options.tracers = {makeTracerRegistry().defaultName()};
    if (options.tracers.size() != 1)
        throw std::invalid_argument("Choose one surface tracer for each comparison condition");
    // Skipping off / on: the same bricks in the encoding without skipping, then in its twin.
    std::string skippingOn;
    if (secondTracer.empty()) {
        if (options.model.empty())
            throw std::invalid_argument("Skipping requires a voxel model");
        const encoding::EncodingRegistry encodings = makeEncodingRegistry();
        if (options.encoding.empty())
            options.encoding = encodings.defaultName();
        const auto chosen = encodings.create(options.encoding);
        if (chosen->skippingTwin().empty())
            throw std::invalid_argument("Encoding '" + options.encoding + "' has no variant with skipping switched");
        skippingOn = chosen->skipsEmptySpace() ? options.encoding : chosen->skippingTwin();
        if (chosen->skipsEmptySpace())
            options.encoding = chosen->skippingTwin();
    } else if (secondTracer == options.tracers.front())
        throw std::invalid_argument("Choose two different tracers");
    // A directory per pair keeps repeated runs independently replayable.
    const auto root = options.outputDirectory;
    std::filesystem::create_directories(root);
    const auto stem = fileTimestamp() + "_fixed";
    for (unsigned i = 0;; ++i) {
        options.outputDirectory = root / (i == 0 ? stem : stem + "_" + std::to_string(i));
        if (std::filesystem::create_directory(options.outputDirectory))
            break;
    }
    ViewComparison result;
    ReferenceFrame referenceHits;
    if (benchmark(options, &result, &referenceHits))
        throw std::runtime_error("Validation failed in the first run");
    options.exactCamera = result.camera;
    if (secondTracer.empty())
        options.encoding = skippingOn;
    else
        options.tracers = {secondTracer};
    if (options.cooldownSeconds > 0)
        std::this_thread::sleep_for(std::chrono::duration<double>(options.cooldownSeconds));
    if (benchmark(options, &result, &referenceHits))
        throw std::runtime_error("Validation failed in the second run");
    std::printf("Fixed view: %zu of %zu rays disagree beyond rounding (%s).\n", result.hits.disagreements(),
                result.hits.rays, result.hits.agrees() ? "agree" : "DISAGREE");
    return result;
}

void restoreResult(Options& options)
{
    std::ifstream input(options.openResult);
    const auto report = nlohmann::json::parse(input);
    const auto& run = report.at("run");
    const auto& config = run.at("config");
    if (config.at("camera_path") != "fixed")
        throw std::invalid_argument("Choose a fixed-view result");
    const auto& pose = config.at("pose");
    const auto& target = pose.at("target");
    core::OrbitCamera camera;
    camera.target = {target.at(0).get<float>(), target.at(1).get<float>(), target.at(2).get<float>()};
    camera.yawDegrees = pose.at("yaw");
    camera.pitchDegrees = pose.at("pitch");
    camera.distance = pose.at("distance");
    camera.verticalFovDegrees = pose.at("fov");
    options.exactCamera = camera;
    options.fixedView = true;
    options.width = config.at("width");
    options.height = config.at("height");
    const auto& scene = run.at("scene");
    options.model = scene.at("model_path").get<std::string>();
    options.boxExample = options.model.empty();
    options.seed = scene.at("seed");
    options.gridSize = scene.at("grid_size");
    options.encoding = scene.at("encoding");
    options.brickSize = scene.at("brick_size");
    options.tracers = {run.at("tracer").get<std::string>()};
}
} // namespace thesis::app
