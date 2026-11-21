#pragma once

// Side: GPU

// What every tracer program has, whether it is a compute shader (TraceKernels) or a ray tracing
// pipeline (TracePipelines): the bindings every tracer gets, the file name of the shader for a
// primitive kind, and the resources of one trace. One place, so both kinds of tracer see the same
// rays, hits, primitive data and work list.

#include "thesis/gpu/ComputeKernel.h"
#include "thesis/tracing/GpuScene.h"
#include "thesis/tracing/RayBatch.h"
#include "thesis/tracing/Tracer.h"

#include <filesystem>
#include <string>
#include <vector>

namespace thesis::tracing {

// Rays, hits, the tracer's own bindings, the work list and ray log (counting only), and the
// primitive kind's data.
[[nodiscard]] std::vector<gpu::KernelBinding> traceBindings(const std::vector<gpu::KernelBinding>& own,
                                                            const GpuScene& scene, bool counting);

// <directory>/<shaderName>.<kind>[.counting].spv
[[nodiscard]] std::filesystem::path traceShaderPath(const TracerEnvironment& environment, const std::string& shaderName,
                                                    const GpuScene& scene, bool counting);

// Adds the rays, hits, work list (if `work`) and primitive data to `resources`.
void bindTraceResources(gpu::KernelResources& resources, const RayBatch& batch, const GpuScene& scene,
                        const WorkOutput* work);

} // namespace thesis::tracing
