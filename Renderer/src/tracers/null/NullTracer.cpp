// ============================================================================
// READING ORDER: FILE 15A (C++ side of the "Null" Baseline Tracer)
// ============================================================================
// Null tracer: a measurement baseline, not a real tracer.
//
// It does the part every tracer must do (read each ray, write a hit) and nothing else; every
// ray reports a miss. Its time is the floor that moving the ray and hit lists through memory
// costs, so a tracer's real search cost is roughly (tracer time - null time).

#include "thesis/gpu/ComputeKernel.h"
#include "thesis/tracing/Bindings.h"
#include "thesis/tracing/Tracer.h"

#include <memory>
#include <optional>
#include <stdexcept>

namespace thesis::tracers {
// C++ REFRESHER: An unnamed namespace (`namespace { ... }`) makes everything inside it private
// to this `.cpp` file (like `static` in C), so `TraceParams` or `kGroupSize` here cannot collide
// with identically named types in `SoftwareTracer.cpp` or `HardwareTracer.cpp`.
namespace {

constexpr std::uint32_t kGroupSize = 64; // matches [numthreads(64, 1, 1)] in trace_null.slang

// Push-constant struct passed to `trace_null.slang`. Must be 16 bytes (4 uint32_t) to match the
// shader's `TraceParams`.
struct TraceParams {
    std::uint32_t rayCount;
    std::uint32_t pad0;
    std::uint32_t pad1;
    std::uint32_t pad2;
};

// `final` means no other class derives from `NullTracer`.
class NullTracer final : public tracing::ITracer {
public:
    explicit NullTracer(const tracing::TracerEnvironment& environment) : environment_(environment) {}

    std::string_view name() const override { return "null"; }

    tracing::PrepareReport prepare(const tracing::GpuScene& /*scene*/) override
    {
        // Load `trace_null.spv` and declare that it uses slot 0 (rays) and slot 1 (hits).
        // `kernel_.emplace(...)` constructs the `ComputeKernel` inside the `std::optional`.
        kernel_.emplace(environment_.gpu,
                        gpu::KernelDescription{environment_.shaderDirectory / "trace_null.spv",
                                               {{tracing::kRayBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                                                {tracing::kHitBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}},
                                               sizeof(TraceParams)});
        return {};
    }

    void trace(VkCommandBuffer cmd, const tracing::RayBatch& batch) override
    {
        if (!kernel_) {
            throw std::logic_error("NullTracer: prepare() must be called before trace()");
        }
        // Plug `batch.rays` into slot 0 and `batch.hits` into slot 1, then launch enough 64-thread
        // workgroups to cover `batch.count()` rays.
        gpu::KernelResources resources;
        resources.buffer(tracing::kRayBinding, batch.rays).buffer(tracing::kHitBinding, batch.hits);
        const TraceParams params{batch.count(), 0, 0, 0};
        kernel_->dispatch(cmd, resources, gpu::groupCount(batch.count(), kGroupSize), 1, 1, params);
    }

    std::uint64_t memoryBytes() const override { return 0; }
    // Tells `TestAgreement.cpp` to skip this tracer when checking that all tracers return the
    // same hits, since the null tracer intentionally misses every ray.
    bool findsHits() const override { return false; }

private:
    tracing::TracerEnvironment environment_;
    // C++ REFRESHER: `std::optional<T>` holds either nothing (`std::nullopt`) or a `T` value
    // constructed later. We use it because `ComputeKernel` has no default constructor and is only
    // created when `prepare()` is called.
    std::optional<gpu::ComputeKernel> kernel_;
};

} // namespace
} // namespace thesis::tracers

namespace thesis::plugins {

// Called automatically by the generated `BuiltInTracers` function (see `cmake/ThesisPlugins.cmake`)
// to register `"null"` in the `TracerRegistry`.
void registerNullTracer(tracing::TracerRegistry& registry)
{
    registry.add("null", "Baseline: reads every ray, writes a miss. Its time is the memory-traffic floor.",
                 [](const tracing::TracerEnvironment& environment) {
                     return std::make_unique<tracers::NullTracer>(environment);
                 });
}

} // namespace thesis::plugins
