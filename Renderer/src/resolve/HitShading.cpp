#include "thesis/resolve/HitShading.h"

#include "thesis/shared/Bindings.h"

namespace thesis::resolve {
namespace {

constexpr std::uint32_t kGroupSize = 8; // matches [numthreads(8, 8, 1)] in shade.slang and shade_work.slang
constexpr std::uint32_t kHitBinding = THESIS_BINDING_SHADE_HITS;
constexpr std::uint32_t kOutputBinding = THESIS_BINDING_SHADE_OUTPUT;
constexpr std::uint32_t kWorkBinding = THESIS_BINDING_SHADE_WORK;

// Mirrors ShadeParams in shade.slang.
struct ShadeParams {
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t mode;
    std::uint32_t rayOrder; // tracing::RayOrder
    float distanceNear;
    float distanceFar;
    float lightAzimuth;
    float lightElevation;
};

// Mirrors ShadeWorkParams in shade_work.slang.
struct ShadeWorkParams {
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t mode; // 0 cells, 1 primitives, 2 runs, 3 kind of the cell at the hit, 4 structure steps, 5 mipmap
                        // cells, 6 largest square skipped
    std::uint32_t scaleKind;
    float scale;
    std::uint32_t rayOrder; // tracing::RayOrder
    float pad2;
    float pad3;
};

} // namespace

HitShading::HitShading(const gpu::Context& context, const std::filesystem::path& shaderDirectory)
    : kernel_(context, gpu::KernelDescription{shaderDirectory / "shade.spv",
                                              {{kHitBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                                               {kOutputBinding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}},
                                              sizeof(ShadeParams)}),
      workKernel_(context, gpu::KernelDescription{shaderDirectory / "shade_work.spv",
                                                  {{kHitBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                                                   {kOutputBinding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
                                                   {kWorkBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}},
                                                  sizeof(ShadeWorkParams)})
{}

void HitShading::shade(VkCommandBuffer cmd, const tracing::RayBatch& batch, VkImageView output,
                       const ShadingParams& params, const gpu::Buffer* work) const
{
    const std::uint32_t groupsX = gpu::groupCount(batch.width, kGroupSize);
    const std::uint32_t groupsY = gpu::groupCount(batch.height, kGroupSize);
    gpu::KernelResources resources;
    resources.buffer(kHitBinding, batch.hits).storageImage(kOutputBinding, output);

    if (showsWork(params.mode) && work != nullptr) {
        const std::uint32_t workMode =
            static_cast<std::uint32_t>(params.mode) - static_cast<std::uint32_t>(ShadingMode::WorkCells);
        const ShadeWorkParams push{batch.width,
                                   batch.height,
                                   workMode,
                                   static_cast<std::uint32_t>(params.workScale.kind),
                                   params.workScale.maximum,
                                   static_cast<std::uint32_t>(batch.order),
                                   0.0f,
                                   0.0f};
        resources.buffer(kWorkBinding, *work);
        workKernel_.dispatch(cmd, resources, groupsX, groupsY, 1, push);
        return;
    }

    const ShadingMode mode = showsWork(params.mode) ? ShadingMode::Shaded : params.mode;
    const ShadeParams push{batch.width,
                           batch.height,
                           static_cast<std::uint32_t>(mode),
                           static_cast<std::uint32_t>(batch.order),
                           params.distanceNear,
                           params.distanceFar,
                           params.light.azimuth,
                           params.light.elevation};
    kernel_.dispatch(cmd, resources, groupsX, groupsY, 1, push);
}

} // namespace thesis::resolve
