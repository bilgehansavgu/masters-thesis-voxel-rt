// The agreement check: identical rays through every tracer must give the same hits.
// This is the regression test for every future tracer and encoding. Needs a GPU with
// hardware ray tracing; runs with the validation layer on and fails on any validation message.

#include "HitComparison.h"

#include "thesis/core/BoxField.h"
#include "thesis/core/Camera.h"
#include "thesis/encodings/BuiltInEncodings.h"
#include "thesis/gpu/Barriers.h"
#include "thesis/gpu/Buffer.h"
#include "thesis/gpu/Context.h"
#include "thesis/mesh/TriangleMesh.h"
#include "thesis/tracers/BuiltInTracers.h"
#include "thesis/tracing/Bindings.h"
#include "thesis/tracing/GpuScene.h"
#include "thesis/tracing/RayBatch.h"
#include "thesis/tracing/Tracer.h"
#include "thesis/voxel/Voxelizer.h"
#include "thesis/workloads/CameraRays.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>
#include <memory>
#include <span>
#include <utility>
#include <vector>

using namespace thesis;

namespace {

constexpr float kRelativeTolerance = 1e-4f;
constexpr double kMaxDisagreementFraction = 1e-4; // at most 1 ray in 10,000
constexpr double kMaxPrimitiveMismatchFraction = 1e-3;

std::vector<core::Hit> traceAndRead(const gpu::Context& context, const workloads::CameraRays& rays,
                                    tracing::ITracer& tracer, const core::CameraFrame& camera,
                                    const tracing::RayBatch& batch)
{
    context.submitAndWait([&](VkCommandBuffer cmd) {
        rays.generate(cmd, camera, batch);
        gpu::computeToComputeBarrier(cmd);
        tracer.trace(cmd, batch);
    });
    const std::vector<std::byte> bytes = gpu::downloadBuffer(context, batch.hits);
    std::vector<core::Hit> hits(batch.count());
    std::memcpy(hits.data(), bytes.data(), hits.size() * sizeof(core::Hit));
    return hits;
}

// Every registered tracer that finds hits (baselines such as the null tracer are skipped),
// prepared for `scene`.
struct HitFindingTracers {
    std::vector<std::string> names;
    std::vector<std::unique_ptr<tracing::ITracer>> tracers;
};

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

// Sends the same camera rays from several viewpoints through every tracer that finds hits and
// compares the hit lists.
void checkTracersAgree(const gpu::Context& context, const core::Scene& scene)
{
    const tracing::GpuScene gpuScene = tracing::GpuScene::upload(context, scene);

    const HitFindingTracers set = makeHitFindingTracers(context, gpuScene);
    const std::vector<std::string>& names = set.names;
    const auto& tracers = set.tracers;
    REQUIRE(names.size() >= 2);

    const workloads::CameraRays cameraRays(context, THESIS_DEFAULT_SHADER_DIR);
    const tracing::RayBatch batch = tracing::RayBatch::create(context, 640, 480);

    const core::OrbitCamera framing = core::OrbitCamera::framing(scene.bounds);
    struct Viewpoint {
        float yaw;
        float pitch;
        float distanceScale;
    };
    const Viewpoint viewpoints[] = {
        {35.0f, 30.0f, 0.7f}, {125.0f, 10.0f, 0.5f}, {220.0f, 60.0f, 0.6f}, {300.0f, 5.0f, 0.35f}, {0.0f, 89.0f, 0.6f}};

    for (const Viewpoint& viewpoint : viewpoints) {
        core::OrbitCamera camera = framing;
        camera.yawDegrees = viewpoint.yaw;
        camera.pitchDegrees = viewpoint.pitch;
        camera.distance *= viewpoint.distanceScale;
        const core::CameraFrame frame = camera.frame(4.0f / 3.0f);

        const std::vector<core::Hit> reference = traceAndRead(context, cameraRays, *tracers[0], frame, batch);
        for (std::size_t i = 1; i < tracers.size(); ++i) {
            const std::vector<core::Hit> hits = traceAndRead(context, cameraRays, *tracers[i], frame, batch);
            const tests::HitComparison result = tests::compareHits(reference, hits, kRelativeTolerance);

            INFO(names[0] << " vs " << names[i] << " at yaw " << viewpoint.yaw << ", pitch " << viewpoint.pitch);
            INFO("both hit " << result.bothHit << ", both miss " << result.bothMiss << ", hit vs miss "
                             << result.hitVersusMiss << ", distance mismatch " << result.distanceMismatch
                             << ", primitive mismatch " << result.primitiveMismatch << ", max relative error "
                             << result.maxRelativeDistanceError);
            const bool inView = result.bothHit > result.rays / 10; // the scene is actually in view
            const bool agree = result.disagreementFraction() <= kMaxDisagreementFraction &&
                               static_cast<double>(result.primitiveMismatch) <=
                                   kMaxPrimitiveMismatchFraction * static_cast<double>(result.rays);
            CHECK(inView);
            CHECK(agree);
            if (!inView || !agree) {
                // Pictures of both hit lists, to see what went wrong.
                const std::string stem = "agreement_yaw" + std::to_string(static_cast<int>(viewpoint.yaw)) + "_";
                tests::writeHitImage(stem + names[0] + ".ppm", reference, batch.width, batch.height);
                tests::writeHitImage(stem + names[i] + ".ppm", hits, batch.width, batch.height);
                WARN("Wrote " << stem << "*.ppm to the working directory");
            }
        }
    }
}

} // namespace

TEST_CASE("all tracers agree on the box field", "[gpu][agreement]")
{
    gpu::ContextOptions options;
    options.applicationName = "Thesis agreement test";
    options.validation = true;
    const gpu::Context context(options);
    const std::uint64_t validationBefore = gpu::validationMessageCount();

    checkTracersAgree(context, core::makeBoxField({}));

    CHECK(gpu::validationMessageCount() == validationBefore);
}

// The brick kinds go through the same check. In step 1 their shader code stops at the box of
// each brick, so this proves the path around it: every tracer finds the shader of each kind,
// and the buffers of each encoding upload, bind and pass the validation layer.
TEST_CASE("all tracers agree on an encoded voxel model", "[gpu][agreement]")
{
    gpu::ContextOptions options;
    options.applicationName = "Thesis agreement test";
    options.validation = true;
    const gpu::Context context(options);
    const std::uint64_t validationBefore = gpu::validationMessageCount();

    const voxel::VoxelModel model =
        voxel::voxelize(mesh::makeSphere({0.0f, 0.0f, 0.0f}, 1.0f, 48), {64, 8, voxel::VoxelizationRule::Sampled});
    encoding::EncodingRegistry encodings;
    plugins::registerBuiltInEncodings(encodings);
    REQUIRE_FALSE(encodings.names().empty());
    for (const std::string& name : encodings.names()) {
        INFO("encoding " << name);
        const core::Scene scene = encodings.create(name)->encode(model).scene;
        REQUIRE(scene.primitiveData.size() >= 4);
        checkTracersAgree(context, scene);
    }

    CHECK(gpu::validationMessageCount() == validationBefore);
}

// Rays with answers worked out by hand. The agreement test above cannot catch a bug in the
// shared box-check code (both tracers would be wrong the same way); this test can.
//
//   box 0: [0,1] x [0,1] x [0,1]        box 1: [3,4] x [0,2] x [0,1]
//
// Faces: 0..5 = -X, +X, -Y, +Y, -Z, +Z (the side the ray enters through).
TEST_CASE("tracers return hand-computed hits", "[gpu][known-answer]")
{
    gpu::ContextOptions options;
    options.applicationName = "Thesis known-answer test";
    options.validation = true;
    const gpu::Context context(options);
    const std::uint64_t validationBefore = gpu::validationMessageCount();

    core::Scene scene;
    scene.name = "two boxes";
    scene.primitiveKind = "box";
    scene.primitiveBounds = {core::Aabb{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}},
                             core::Aabb{{3.0f, 0.0f, 0.0f}, {4.0f, 2.0f, 1.0f}}};
    for (const core::Aabb& box : scene.primitiveBounds) {
        scene.bounds.extend(box);
    }
    // Extra primitive data the box shader does not read. It checks that tracers pass a kind's
    // own buffers through without knowing about them (what the brick encoding will rely on).
    scene.primitiveData.push_back({tracing::kPrimitiveDataBindingFirst, "unused", std::vector<std::byte>(64)});
    const tracing::GpuScene gpuScene = tracing::GpuScene::upload(context, scene);
    REQUIRE(gpuScene.primitiveBindings().size() == 2);

    struct Case {
        const char* what;
        core::Ray ray;
        std::uint32_t primitive;
        float t;
        std::uint32_t face;
    };
    constexpr float kFar = 1.0e30f;
    const float diagonal = std::sqrt(0.5f);
    const Case cases[] = {
        {"along +X into box 0", {{-1.0f, 0.5f, 0.5f}, 0.0f, {1.0f, 0.0f, 0.0f}, kFar}, 0, 1.0f, 0},
        {"down onto the top of box 1", {{3.5f, 5.0f, 0.5f}, 0.0f, {0.0f, -1.0f, 0.0f}, kFar}, 1, 3.0f, 3},
        {"from between the boxes, +X", {{2.0f, 0.5f, 0.5f}, 0.0f, {1.0f, 0.0f, 0.0f}, kFar}, 1, 1.0f, 0},
        {"from between the boxes, -X", {{2.0f, 0.5f, 0.5f}, 0.0f, {-1.0f, 0.0f, 0.0f}, kFar}, 0, 1.0f, 1},
        {"along +Z into box 0", {{0.5f, 0.5f, -2.0f}, 0.0f, {0.0f, 0.0f, 1.0f}, kFar}, 0, 2.0f, 4},
        {"diagonal, enters box 0 through -X",
         {{-1.0f, -0.5f, 0.5f}, 0.0f, {diagonal, diagonal, 0.0f}, kFar},
         0,
         std::sqrt(2.0f),
         0},
        {"passes above both boxes", {{-1.0f, 5.0f, 0.5f}, 0.0f, {1.0f, 0.0f, 0.0f}, kFar}, core::kNoHit, 0.0f, 0},
        {"stops short (tMax)", {{-1.0f, 0.5f, 0.5f}, 0.0f, {1.0f, 0.0f, 0.0f}, 0.5f}, core::kNoHit, 0.0f, 0},
        {"starts past box 0 (tMin)", {{-1.0f, 0.5f, 0.5f}, 2.5f, {1.0f, 0.0f, 0.0f}, kFar}, 1, 4.0f, 0},
    };

    std::vector<core::Ray> rays;
    for (const Case& c : cases) {
        rays.push_back(c.ray);
    }
    tracing::RayBatch batch;
    batch.width = static_cast<std::uint32_t>(rays.size());
    batch.height = 1;
    batch.rays = gpu::uploadBuffer(context, std::as_bytes(std::span(rays)), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    batch.hits = gpu::Buffer(context, rays.size() * sizeof(core::Hit), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                             gpu::MemoryUsage::DeviceLocal);

    const HitFindingTracers set = makeHitFindingTracers(context, gpuScene);
    REQUIRE_FALSE(set.tracers.empty());
    for (std::size_t t = 0; t < set.tracers.size(); ++t) {
        context.submitAndWait([&](VkCommandBuffer cmd) { set.tracers[t]->trace(cmd, batch); });
        const std::vector<std::byte> bytes = gpu::downloadBuffer(context, batch.hits);
        std::vector<core::Hit> hits(rays.size());
        std::memcpy(hits.data(), bytes.data(), bytes.size());

        for (std::size_t i = 0; i < std::size(cases); ++i) {
            const Case& expected = cases[i];
            INFO(set.names[t] << ": " << expected.what);
            CHECK(hits[i].primitive == expected.primitive);
            if (expected.primitive != core::kNoHit) {
                CHECK(hits[i].t == Catch::Approx(expected.t).epsilon(1e-6));
                CHECK(hits[i].face == expected.face);
            }
        }
    }

    CHECK(gpu::validationMessageCount() == validationBefore);
}

TEST_CASE("scene rejects primitive data outside its binding range", "[gpu]")
{
    const gpu::Context context(gpu::ContextOptions{});
    core::Scene scene = core::makeBoxField({.gridSize = 2});

    scene.primitiveData = {{tracing::kPrimitiveBoundsBinding, "clashes with bounds", std::vector<std::byte>(16)}};
    CHECK_THROWS(tracing::GpuScene::upload(context, scene));

    scene.primitiveData = {{tracing::kPrimitiveDataBindingLast + 1, "too high", std::vector<std::byte>(16)}};
    CHECK_THROWS(tracing::GpuScene::upload(context, scene));

    scene.primitiveData = {{9, "a", std::vector<std::byte>(16)}, {9, "b", std::vector<std::byte>(16)}};
    CHECK_THROWS(tracing::GpuScene::upload(context, scene));

    scene.primitiveData = {{9, "a", std::vector<std::byte>(16)}, {15, "b", std::vector<std::byte>(16)}};
    CHECK_NOTHROW(tracing::GpuScene::upload(context, scene));
}
