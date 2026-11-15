// ============================================================================
// READING ORDER: FILE 15B (C++ side of the Software Tracer)
// ============================================================================
// Software tracer: a compute shader with no help from the ray tracing hardware.
//
// Step 0 version: every ray is tested against every primitive. Slow on purpose; it is the
// simplest correct answer. Step 3 replaces the loop with our own search structure over bricks.

#include "thesis/gpu/ComputeKernel.h"
#include "thesis/tracing/Bindings.h"
#include "thesis/tracing/Tracer.h"

#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace thesis::tracers {
namespace {

constexpr std::uint32_t kGroupSize = 64; // matches [numthreads(64, 1, 1)] in trace_software.slang

struct TraceParams {
    std::uint32_t rayCount;
    std::uint32_t primitiveCount;
    std::uint32_t pad0;
    std::uint32_t pad1;
};

class SoftwareTracer final : public tracing::ITracer {
public:
    explicit SoftwareTracer(const tracing::TracerEnvironment& environment) : environment_(environment) {}

    std::string_view name() const override { return "software"; }

    tracing::PrepareReport prepare(const tracing::GpuScene& scene) override
    {
        scene_ = &scene;

        // Collect all descriptor slots the compute shader needs:
        //   - Slot 0: input ray list
        //   - Slot 1: output hit list
        //   - Slots 8..15: whatever buffers the scene's `primitiveKind` uses (from `primitiveBindings()`)
        std::vector<gpu::KernelBinding> bindings{{tracing::kRayBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                                                 {tracing::kHitBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}};
        for (const gpu::KernelBinding& binding : scene.primitiveBindings()) {
            bindings.push_back(binding);
        }

        // Notice the shader filename: `"trace_software." + scene.primitiveKind + ".spv"`.
        // CMake compiles `trace_software.slang` once per primitive kind (e.g., `trace_software.box.spv`).
        kernel_.emplace(environment_.gpu, gpu::KernelDescription{environment_.shaderDirectory /
                                                                     ("trace_software." + scene.primitiveKind + ".spv"),
                                                                 std::move(bindings), sizeof(TraceParams)});
        return {}; // Step 0 brute-force loop has no search structure to build yet
    }

    void trace(VkCommandBuffer cmd, const tracing::RayBatch& batch) override
    {
        if (!kernel_ || scene_ == nullptr) {
            throw std::logic_error("SoftwareTracer: prepare() must be called before trace()");
        }
        gpu::KernelResources resources;
        resources.buffer(tracing::kRayBinding, batch.rays).buffer(tracing::kHitBinding, batch.hits);
        // Attach slot 8 (`primitiveBounds`) and any extra primitive data buffers (slots 9..15).
        scene_->bindPrimitiveData(resources);
        const TraceParams params{batch.count(), scene_->primitiveCount, 0, 0};
        kernel_->dispatch(cmd, resources, gpu::groupCount(batch.count(), kGroupSize), 1, 1, params);
    }

    std::uint64_t memoryBytes() const override { return 0; }

private:
    tracing::TracerEnvironment environment_;
    const tracing::GpuScene* scene_ = nullptr;
    std::optional<gpu::ComputeKernel> kernel_;
};

} // namespace
} // namespace thesis::tracers

namespace thesis::plugins {

void registerSoftwareTracer(tracing::TracerRegistry& registry)
{
    registry.add("software", "Compute shader, no ray tracing hardware. Step 0: tests every primitive per ray.",
                 [](const tracing::TracerEnvironment& environment) {
                     return std::make_unique<tracers::SoftwareTracer>(environment);
                 });
}

} // namespace thesis::plugins
