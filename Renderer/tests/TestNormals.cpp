// The packed normal is decoded in two places: in C++ (src/voxel/OctNormal.cpp) and in the
// shading shader (shaders/common/normals.slang). This test runs the shader code for all 65,536
// values and compares each result with the C++ code. Needs a GPU.

#include "GpuTestHelpers.h"

#include "thesis/gpu/Buffer.h"
#include "thesis/gpu/ComputeKernel.h"
#include "thesis/voxel/OctNormal.h"

#include <catch2/catch_test_macros.hpp>
#include <glm/glm.hpp>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <vector>

using namespace thesis;

TEST_CASE("the shader decodes every packed normal like the C++ code", "[gpu][normals]")
{
    const gpu::Context context(tests::testContextOptions("Thesis normal test"));
    const std::uint64_t validationBefore = gpu::validationMessageCount();

    constexpr std::uint32_t kValues = 65536;
    const gpu::ComputeKernel kernel(
        context, gpu::KernelDescription{std::filesystem::path(THESIS_DEFAULT_SHADER_DIR) / "decode_normals.spv",
                                        {{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}},
                                        0});
    const gpu::Buffer output(context, kValues * sizeof(glm::vec4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                             gpu::MemoryUsage::DeviceLocal);
    context.submitAndWait([&](VkCommandBuffer cmd) {
        gpu::KernelResources resources;
        resources.buffer(0, output);
        kernel.dispatch(cmd, resources, gpu::groupCount(kValues, 64), 1, 1);
    });
    const std::vector<std::byte> bytes = gpu::downloadBuffer(context, output);
    std::vector<glm::vec4> decoded(kValues);
    std::memcpy(decoded.data(), bytes.data(), kValues * sizeof(glm::vec4));

    float largestDifference = 0.0f;
    for (std::uint32_t packed = 0; packed < kValues; ++packed) {
        const glm::vec3 expected = voxel::decodeNormal(static_cast<std::uint16_t>(packed));
        largestDifference = std::max(largestDifference, glm::distance(glm::vec3(decoded[packed]), expected));
    }
    CHECK(largestDifference < 1e-5f);

    CHECK(gpu::validationMessageCount() == validationBefore);
}
