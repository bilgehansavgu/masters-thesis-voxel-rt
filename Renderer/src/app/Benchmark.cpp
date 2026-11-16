#include "Benchmark.h"

#include "FramePipeline.h"
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
// how many primitives, cells and runs a ray was tested against. It runs the counting variant
// of the tracer's shader. That variant is slower, so this happens after the timed repeats and
// takes no part in them.
nlohmann::json measureRayWork(const gpu::Context& context, FramePipeline& pipeline, tracing::ITracer& tracer,
                              const core::OrbitCamera& baseCamera)
{
    constexpr std::uint32_t kWorkViews = 8;
    const std::size_t count = pipeline.batch().count();
    std::vector<core::RayWork> work(count);
    std::vector<core::Hit> hits(count);

    std::uint64_t rays = 0;
    std::uint64_t raysWithHit = 0;
    core::RayWork most{};
    // Sums over all rays [0] and over the rays with a hit [1].
    std::array<std::uint64_t, 2> primitiveTests{};
    std::array<std::uint64_t, 2> cells{};
    std::array<std::uint64_t, 2> runs{};
    std::array<std::uint64_t, 4> hitCells{};
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

        for (std::size_t ray = 0; ray < count; ++ray) {
            const bool hit = hits[ray].primitive != core::kNoHit;
            for (std::size_t group = 0; group < (hit ? 2u : 1u); ++group) {
                primitiveTests[group] += work[ray].primitiveTests;
                cells[group] += work[ray].cells;
                runs[group] += work[ray].runs;
            }
            most.primitiveTests = std::max(most.primitiveTests, work[ray].primitiveTests);
            most.cells = std::max(most.cells, work[ray].cells);
            most.runs = std::max(most.runs, work[ray].runs);
            raysWithHit += hit ? 1 : 0;
            ++hitCells[std::min<std::uint32_t>(work[ray].hitCell, THESIS_HIT_CELL_SEVERAL_RUNS)];
        }
        rays += count;
    }

    const auto mean = [&](const std::array<std::uint64_t, 2>& sums, std::size_t group) {
        const std::uint64_t count = group == 0 ? rays : raysWithHit;
        return count == 0 ? 0.0 : static_cast<double>(sums[group]) / static_cast<double>(count);
    };
    const auto means = [&](std::size_t group) {
        return nlohmann::json{{"primitive_tests", mean(primitiveTests, group)},
                              {"cells", mean(cells, group)},
                              {"runs", mean(runs, group)}};
    };
    return {{"note", "from the counting variant of the shader, not timed"},
            {"views", kWorkViews},
            {"rays", rays},
            {"rays_with_hit", raysWithHit},
            {"mean_per_ray", means(0)},
            {"mean_per_ray_with_hit", means(1)},
            {"max_per_ray", {{"primitive_tests", most.primitiveTests}, {"cells", most.cells}, {"runs", most.runs}}},
            {"hits_by_cell_kind",
             {{"height", hitCells[THESIS_HIT_CELL_HEIGHT]},
              {"one_run", hitCells[THESIS_HIT_CELL_ONE_RUN]},
              {"several_runs", hitCells[THESIS_HIT_CELL_SEVERAL_RUNS]}}}};
}

struct WarmupResult {
    double seconds = 0.0;
    std::uint32_t frames = 0;
    bool clockStable = false;
    std::optional<std::uint32_t> clockMhz;
};

// Renders until at least `minSeconds` have passed and the GPU clock has settled (kStableReadings
// readings, kSamplePeriod apart, each within 1 % of the previous), or until `maxSeconds`.
// A fixed time, not a frame count: 60 frames are seconds for a slow tracer but milliseconds for
// a fast one, far too short for a laptop GPU to reach its working clock.
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
    WarmupResult result;

    while (true) {
        (void)renderFrame(result.frames % framesPerOrbit);
        ++result.frames;

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
        const bool settled = result.clockStable || !lastClock.has_value(); // unreadable clock: time only
        if ((result.seconds >= minSeconds && settled) || result.seconds >= maxSeconds) {
            return result;
        }
    }
}

} // namespace

int runBenchmark(const Options& options)
{
    gpu::ContextOptions contextOptions;
    contextOptions.applicationName = "Thesis Renderer (benchmark)";
    contextOptions.validation = options.validation;
    const gpu::Context context(contextOptions);
    if (options.validation) {
        std::printf("note: validation is on, timings are not representative\n");
    }

    const SceneSource source(options);
    const BuiltScene built = source.build(source.initialEncoding());
    const core::Scene& scene = built.scene;
    const tracing::GpuScene gpuScene = tracing::GpuScene::upload(context, scene);
    const tracing::TracerRegistry registry = makeTracerRegistry();
    const std::vector<std::string> tracerNames = selectTracers(registry, options.tracers);
    const tracing::TracerEnvironment environment{context, options.shaderDirectory};

    FramePipeline pipeline(context, options.shaderDirectory, {options.width, options.height});
    const core::OrbitCamera baseCamera = startCamera(options, scene.bounds);
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
    std::printf("Protocol: warm-up %.0f-%.0f s until the GPU clock settles, then %u repeats x %u frames\n",
                options.warmupSeconds, options.warmupMaxSeconds, options.repeats, options.frames);
    std::printf("Camera: orbit from yaw %.0f, pitch %.0f, at %.2f x the distance that shows the whole scene\n\n",
                options.camera.at(0), options.camera.at(1), options.camera.at(2));
    std::printf("%-10s %10s %10s %10s %10s %10s %8s\n", "tracer", "median ms", "min ms", "max ms", "Mrays/s",
                "clock MHz", "settled");

    // Medians of tracers that find hits, and the fastest baseline (null tracer), for the summary.
    std::vector<std::pair<std::string, double>> tracerMedians;
    std::optional<double> floorMs;

    for (std::size_t index = 0; index < tracerNames.size(); ++index) {
        const std::string& tracerName = tracerNames[index];
        if (index > 0 && options.cooldownSeconds > 0.0) {
            std::this_thread::sleep_for(std::chrono::duration<double>(options.cooldownSeconds));
        }

        const auto tracer = registry.create(tracerName, environment);
        tracing::PrepareReport prepared;
        try {
            prepared = tracer->prepare(gpuScene);
        } catch (const tracing::UnsupportedScene& reason) {
            if (!options.tracers.empty()) {
                throw; // the user asked for this tracer by name
            }
            std::printf("%-10s skipped: %s\n", tracerName.c_str(), reason.what());
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
                        {"voxels", built.voxels},
                        {"encode_seconds", built.encodeSeconds},
                        {"memory", built.memory.toJson(built.voxels)},
                        {"statistics", built.statistics}};
        run["config"] = {{"width", options.width},
                         {"height", options.height},
                         {"rays_per_frame", rayCount},
                         {"warmup_min_seconds", options.warmupSeconds},
                         {"warmup_max_seconds", options.warmupMaxSeconds},
                         {"frames_per_repeat", options.frames},
                         {"repeats", options.repeats},
                         {"camera_path", "orbit, 360 degrees per repeat"},
                         {"camera_start",
                          {{"yaw_degrees", options.camera.at(0)},
                           {"pitch_degrees", options.camera.at(1)},
                           {"distance_factor", options.camera.at(2)}}},
                         {"validation", options.validation}};
        run["prepare"] = {{"gpu_ms", prepared.gpuMilliseconds},
                          {"cpu_ms", prepared.cpuMilliseconds},
                          {"memory_bytes", prepared.memoryBytes},
                          {"scratch_bytes", prepared.scratchBytes}};
        run["memory_bytes"] = {{"scene", gpuScene.memoryBytes()},
                               {"tracer", tracer->memoryBytes()},
                               {"rays_and_hits", pipeline.batch().memoryBytes()},
                               {"process_device_local", context.deviceLocalUsage()}};
        const auto renderFrame = [&](std::uint32_t frame) {
            THESIS_ZONE("benchmark frame");
            const core::CameraFrame camera = cameraAt(baseCamera, frame, options.frames, pipeline.aspectRatio());
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
                         {"graphics_clock_mhz", warmup.clockMhz ? nlohmann::json(*warmup.clockMhz) : nlohmann::json()}};
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
            allTraceMs.insert(allTraceMs.end(), traceMs.begin(), traceMs.end());
        }

        const core::Summary frames = core::summarize(allTraceMs);
        const core::Summary repeats = core::summarize(repeatMedians);
        const double medianMs = repeats.median;
        const double megaRaysPerSecond = medianMs > 0.0 ? static_cast<double>(rayCount) / (medianMs * 1e3) : 0.0;

        nlohmann::json summary;
        summary["trace_ms"] = {
            {"median_of_repeat_medians", medianMs},
            {"repeat_medians", repeatMedians},
            {"spread_between_repeats_percent", medianMs > 0.0 ? 100.0 * (repeats.max - repeats.min) / medianMs : 0.0},
            {"all_frames",
             {{"median", frames.median},
              {"mean", frames.mean},
              {"min", frames.min},
              {"max", frames.max},
              {"stddev", frames.standardDeviation}}}};
        summary["mrays_per_second"] = megaRaysPerSecond;
        summary["graphics_clock_mhz_after_each_repeat"] = repeatClocks;
        summary["system_after"] = metrics::captureSystemState();
        if (tracer->countsWork()) {
            summary["ray_work"] = measureRayWork(context, pipeline, *tracer, baseCamera);
        }
        sinks.endRun(summary);

        const std::string clockText = warmup.clockMhz ? std::to_string(*warmup.clockMhz) : "n/a";
        std::printf("%-10s %10.3f %10.3f %10.3f %10.1f %10s %8s\n", tracerName.c_str(), medianMs, frames.min,
                    frames.max, megaRaysPerSecond, clockText.c_str(), warmup.clockStable ? "yes" : "NO");
        if (summary.contains("ray_work")) {
            const nlohmann::json& work = summary["ray_work"];
            const nlohmann::json& mean = work["mean_per_ray_with_hit"];
            std::printf(
                "           %.1f %% of the rays hit; a ray that hits: %.2f primitive tests, %.2f cells, %.2f runs\n",
                100.0 * work["rays_with_hit"].get<double>() / work["rays"].get<double>(),
                mean["primitive_tests"].get<double>(), mean["cells"].get<double>(), mean["runs"].get<double>());
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
            std::printf("  %-10s %10.3f ms\n", name.c_str(), median - *floorMs);
        }
    }

    if (options.validation && gpu::validationMessageCount() > 0) {
        std::fprintf(stderr, "\n%llu validation message(s) were reported.\n",
                     static_cast<unsigned long long>(gpu::validationMessageCount()));
        return 2;
    }
    return 0;
}

} // namespace thesis::app
