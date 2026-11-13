#pragma once

#include "thesis/gpu/ComputeKernel.h"
#include "thesis/tracing/RayBatch.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <filesystem>

namespace thesis::resolve {

enum class ShadingMode : std::uint32_t {
    Shaded = 0,      // simple lighting, colour per primitive
    Distance = 1,    // hit distance as grey (near = white)
    PrimitiveId = 2, // flat colour per primitive
    Normal = 3,      // face normal as colour
};

inline constexpr std::array<const char*, 4> kShadingModeNames{"Shaded", "Distance", "Primitive id", "Normal"};

struct ShadingParams {
    ShadingMode mode = ShadingMode::Shaded;
    float distanceNear = 0.0f;
    float distanceFar = 50.0f;
};

// Turns the hit list into an image. Shared by all tracers, so they are judged on identical output.
class HitShading {
public:
    HitShading(const gpu::Context& context, const std::filesystem::path& shaderDirectory);

    // `output` must be an RGBA8 storage image of batch.width × batch.height in GENERAL layout.
    void shade(VkCommandBuffer cmd, const tracing::RayBatch& batch, VkImageView output,
               const ShadingParams& params) const;

private:
    gpu::ComputeKernel kernel_;
};

} // namespace thesis::resolve
