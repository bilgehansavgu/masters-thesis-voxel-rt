#include "GpuTestHelpers.h"

#include "thesis/gpu/Buffer.h"
#include "thesis/tracers/BuiltInTracers.h"
#include "thesis/tracing/RayLogBuffer.h"

#include <cstring>
#include <utility>

namespace thesis::tests {
namespace {

template <typename T>
std::vector<T> download(const gpu::Context& context, const gpu::Buffer& buffer, std::size_t count)
{
    const std::vector<std::byte> bytes = gpu::downloadBuffer(context, buffer);
    std::vector<T> values(count);
    std::memcpy(values.data(), bytes.data(), count * sizeof(T));
    return values;
}

} // namespace

gpu::ContextOptions testContextOptions(const char* name)
{
    gpu::ContextOptions options;
    options.applicationName = name;
    options.validation = true;
    return options;
}

HitFindingTracers makeHitFindingTracers(const gpu::Context& context, const tracing::GpuScene& scene)
{
    tracing::TracerRegistry registry;
    plugins::registerBuiltInTracers(registry);
    const tracing::TracerEnvironment environment{context, THESIS_DEFAULT_SHADER_DIR};
    HitFindingTracers result;
    for (const std::string& name : registry.names()) {
        auto tracer = registry.create(name, environment);
        if (!tracer->findsHits()) {
            continue;
        }
        tracer->prepare(scene);
        result.names.push_back(name);
        result.tracers.push_back(std::move(tracer));
    }
    return result;
}

tracing::RayBatch uploadRays(const gpu::Context& context, std::span<const core::Ray> rays)
{
    tracing::RayBatch batch;
    batch.width = static_cast<std::uint32_t>(rays.size());
    batch.height = 1;
    batch.rays = gpu::uploadBuffer(context, std::as_bytes(rays), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    batch.hits = gpu::Buffer(context, rays.size() * sizeof(core::Hit), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                             gpu::MemoryUsage::DeviceLocal);
    return batch;
}

std::vector<core::Ray> generateRays(const gpu::Context& context, const workloads::CameraRays& cameraRays,
                                    const core::CameraFrame& camera, const tracing::RayBatch& batch)
{
    context.submitAndWait([&](VkCommandBuffer cmd) { cameraRays.generate(cmd, camera, batch); });
    return download<core::Ray>(context, batch.rays, batch.count());
}

std::vector<core::Hit> traceRays(const gpu::Context& context, tracing::ITracer& tracer, const tracing::RayBatch& batch)
{
    context.submitAndWait([&](VkCommandBuffer cmd) { tracer.trace(cmd, batch); });
    return download<core::Hit>(context, batch.hits, batch.count());
}

std::vector<core::Hit> traceRaysCounting(const gpu::Context& context, tracing::ITracer& tracer,
                                         const tracing::RayBatch& batch, std::vector<core::RayWork>& work,
                                         std::uint32_t loggedRay, tracing::RayLog* log)
{
    const gpu::Buffer workBuffer(context, static_cast<VkDeviceSize>(batch.count()) * sizeof(core::RayWork),
                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, gpu::MemoryUsage::DeviceLocal);
    const tracing::RayLogBuffer logBuffer(context);
    context.submitAndWait([&](VkCommandBuffer cmd) {
        logBuffer.request(cmd, loggedRay);
        tracer.traceCounting(cmd, batch, tracing::WorkOutput{workBuffer, logBuffer.buffer()});
        logBuffer.copyBack(cmd);
    });
    if (log != nullptr) {
        *log = logBuffer.read();
    }
    work = download<core::RayWork>(context, workBuffer, batch.count());
    return download<core::Hit>(context, batch.hits, batch.count());
}

core::CameraFrame cameraFrame(const core::Bounds& bounds, const Viewpoint& viewpoint, float aspectRatio)
{
    core::OrbitCamera camera = core::OrbitCamera::framing(bounds);
    camera.yawDegrees = viewpoint.yaw;
    camera.pitchDegrees = viewpoint.pitch;
    camera.distance *= viewpoint.distanceScale;
    return camera.frame(aspectRatio);
}

} // namespace thesis::tests
