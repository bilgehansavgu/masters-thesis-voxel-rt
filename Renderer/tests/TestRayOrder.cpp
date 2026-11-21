// The ray order (thesis/shared/RayOrder.h) decides where the ray of a pixel sits in the ray list.
// It may change which pixels the GPU traces together, never what a pixel shows.

#include "GpuTestHelpers.h"

#include "thesis/core/BoxField.h"
#include "thesis/gpu/Context.h"
#include "thesis/gpu/Image.h"
#include "thesis/resolve/HitShading.h"
#include "thesis/shared/RayOrder.h"
#include "thesis/tracing/RayBatch.h"
#include "thesis/workloads/CameraRays.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <vector>

using namespace thesis;

namespace {

// Image sizes with full tiles, with cut tiles at the right and bottom edge, and smaller than one tile.
constexpr std::uint32_t kSizes[][2] = {{1, 1}, {7, 3}, {8, 4}, {13, 9}, {64, 1}, {1, 64}, {640, 480}, {641, 479}};

} // namespace

TEST_CASE("every ray order gives each pixel its own ray", "[core]")
{
    for (const std::uint32_t order : {std::uint32_t{THESIS_RAY_ORDER_ROWS}, std::uint32_t{THESIS_RAY_ORDER_TILES}}) {
        for (const auto& size : kSizes) {
            const std::uint32_t width = size[0];
            const std::uint32_t height = size[1];
            INFO("order " << order << ", " << width << " x " << height);
            std::vector<std::uint32_t> pixelsOfRay(static_cast<std::size_t>(width) * height, 0);
            bool inside = true;
            for (std::uint32_t y = 0; y < height; ++y) {
                for (std::uint32_t x = 0; x < width; ++x) {
                    const std::uint32_t ray = THESIS_RAY_INDEX(order, x, y, width, height);
                    if (ray >= pixelsOfRay.size()) {
                        inside = false;
                        continue;
                    }
                    ++pixelsOfRay[ray];
                }
            }
            CHECK(inside);
            std::size_t notOnce = 0;
            for (const std::uint32_t count : pixelsOfRay) {
                notOnce += count != 1 ? 1 : 0;
            }
            CHECK(notOnce == 0);
        }
    }
}

TEST_CASE("tiles of 8 x 4 pixels are 32 rays in a row", "[core]")
{
    // The pixels of the tile at (8, 4) of a 640 x 480 image: rays 32 * (tile number) and on.
    const std::uint32_t first = THESIS_RAY_INDEX(THESIS_RAY_ORDER_TILES, 8u, 4u, 640u, 480u);
    CHECK(first == 640u * 4u + 8u * 4u);
    for (std::uint32_t y = 4; y < 8; ++y) {
        for (std::uint32_t x = 8; x < 16; ++x) {
            CHECK(THESIS_RAY_INDEX(THESIS_RAY_ORDER_TILES, x, y, 640u, 480u) - first == (y - 4) * 8 + (x - 8));
        }
    }
}

// The same camera in both orders: each pixel gets the same ray and the same hit, and the shaded
// image is the same, byte for byte.
TEST_CASE("the ray order does not change the image", "[gpu][ray-order]")
{
    const gpu::Context context(tests::testContextOptions("Thesis ray order test"));
    const std::uint64_t validationBefore = gpu::validationMessageCount();
    const core::Scene scene = core::makeBoxField({});
    const tracing::GpuScene gpuScene = tracing::GpuScene::upload(context, scene);
    tests::HitFindingTracers set = tests::makeHitFindingTracers(context, gpuScene);
    REQUIRE_FALSE(set.tracers.empty());
    const workloads::CameraRays cameraRays(context, THESIS_DEFAULT_SHADER_DIR);
    const resolve::HitShading shading(context, THESIS_DEFAULT_SHADER_DIR);

    for (const auto& size : {kSizes[6], kSizes[7]}) {
        const std::uint32_t width = size[0];
        const std::uint32_t height = size[1];
        INFO(width << " x " << height);
        const core::CameraFrame camera = tests::cameraFrame(scene.bounds, {35.0f, 30.0f, 0.7f},
                                                            static_cast<float>(width) / static_cast<float>(height));

        struct Frame {
            std::vector<core::Ray> rays;
            std::vector<core::Hit> hits;
            std::vector<std::byte> image;
        };
        const auto render = [&](tracing::RayOrder order) {
            const tracing::RayBatch batch = tracing::RayBatch::create(context, width, height, order);
            Frame frame;
            frame.rays = tests::generateRays(context, cameraRays, camera, batch);
            frame.hits = tests::traceRays(context, *set.tracers.front(), batch);
            gpu::StorageImage image(context, {width, height}, VK_FORMAT_R8G8B8A8_UNORM);
            context.submitAndWait([&](VkCommandBuffer cmd) {
                image.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                 VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
                shading.shade(cmd, batch, image.view(), resolve::ShadingParams{});
            });
            frame.image = gpu::downloadImage(context, image);
            return frame;
        };
        const Frame rows = render(tracing::RayOrder::Rows);
        const Frame tiles = render(tracing::RayOrder::Tiles);

        std::size_t differentRays = 0;
        std::size_t differentHits = 0;
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                const std::uint32_t r = THESIS_RAY_INDEX(THESIS_RAY_ORDER_ROWS, x, y, width, height);
                const std::uint32_t t = THESIS_RAY_INDEX(THESIS_RAY_ORDER_TILES, x, y, width, height);
                differentRays += std::memcmp(&rows.rays[r], &tiles.rays[t], sizeof(core::Ray)) != 0 ? 1 : 0;
                differentHits += std::memcmp(&rows.hits[r], &tiles.hits[t], sizeof(core::Hit)) != 0 ? 1 : 0;
            }
        }
        CHECK(differentRays == 0);
        CHECK(differentHits == 0);
        CHECK(rows.image == tiles.image);
    }
    CHECK(gpu::validationMessageCount() == validationBefore);
}
