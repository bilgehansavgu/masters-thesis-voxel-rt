#include "Benchmark.h"

#include "FramePipeline.h"
#include "SceneSetup.h"

#include "thesis/core/Statistics.h"
#include "thesis/gpu/Context.h"
#include "thesis/metrics/Sinks.h"
#include "thesis/metrics/SystemInfo.h"
#include "thesis/profiling/Profiling.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
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
    const core::OrbitCamera baseCamera = core::OrbitCamera::framing(scene.bounds);
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
    std::printf("Protocol: warm-up %.0f-%.0f s until the GPU clock settles, then %u repeats x %u frames\n\n",
                options.warmupSeconds, options.warmupMaxSeconds, options.repeats, options.frames);
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
                         {"validation", options.validation}};
        run["prepare"] = {{"gpu_ms", prepared.gpuMilliseconds},
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
        sinks.endRun(summary);

        const std::string clockText = warmup.clockMhz ? std::to_string(*warmup.clockMhz) : "n/a";
        std::printf("%-10s %10.3f %10.3f %10.3f %10.1f %10s %8s\n", tracerName.c_str(), medianMs, frames.min,
                    frames.max, megaRaysPerSecond, clockText.c_str(), warmup.clockStable ? "yes" : "NO");
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
