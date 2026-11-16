#pragma once

#include "thesis/gpu/Buffer.h"
#include "thesis/gpu/ComputeKernel.h"
#include "thesis/tracing/RayBatch.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <filesystem>

namespace thesis::resolve {

enum class ShadingMode : std::uint32_t {
    Shaded = 0,      // lighting with the stored normal; a box has none and gets the normal of its face
    Distance = 1,    // hit distance as grey (near = white)
    PrimitiveId = 2, // flat colour per primitive (for a voxel model: per brick)
    Normal = 3,      // the stored normal as colour
    Faces = 4,       // lighting with the normal of the voxel face that was hit: shows the voxels
    // The views below show the work of each ray. They need the counting variant of a tracer.
    WorkCells = 5,      // cells of primary grids the ray visited
    WorkPrimitives = 6, // primitives the ray was tested against
    WorkRuns = 7,       // runs the ray was tested against
    WorkHitCell = 8,    // kind of the cell with the hit (figure 7 of Reichl et al. 2012)
};

inline constexpr std::array<const char*, 9> kShadingModeNames{"Shaded",
                                                              "Distance",
                                                              "Primitive id",
                                                              "Stored normal",
                                                              "Voxel faces",
                                                              "Work: cells visited",
                                                              "Work: primitives tested",
                                                              "Work: runs tested",
                                                              "Work: cell kind at the hit"};

// True for the views that show the work counters.
[[nodiscard]] constexpr bool showsWork(ShadingMode mode)
{
    return mode >= ShadingMode::WorkCells;
}

struct ShadingParams {
    ShadingMode mode = ShadingMode::Shaded;
    float distanceNear = 0.0f;
    float distanceFar = 50.0f;
    float workScale = 64.0f; // work views: the count that gets the last colour of the scale
};

// Turns the hit list into an image. Shared by all tracers, so they are judged on identical output.
class HitShading {
public:
    HitShading(const gpu::Context& context, const std::filesystem::path& shaderDirectory);

    // `output` must be an RGBA8 storage image of batch.width × batch.height in GENERAL layout.
    // `work` (core::RayWork for each ray) is needed for the work views. Without it they fall
    // back to the shaded view.
    void shade(VkCommandBuffer cmd, const tracing::RayBatch& batch, VkImageView output, const ShadingParams& params,
               const gpu::Buffer* work = nullptr) const;

private:
    gpu::ComputeKernel kernel_;
    gpu::ComputeKernel workKernel_;
};

} // namespace thesis::resolve
