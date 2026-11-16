// Tests for the brick test: the code that finds where a ray hits the voxels inside a brick.
// Every tracer runs that same code, so the agreement test (TestAgreement.cpp) cannot find a bug
// in it. These tests can: they compare the tracers with answers that come from somewhere else.
// Needs a GPU with hardware ray tracing; runs with the validation layer on.

#include "CubeByCubeTracer.h"
#include "GpuTestHelpers.h"
#include "HitComparison.h"
#include "VoxelTestHelpers.h"

#include "thesis/encodings/BuiltInEncodings.h"
#include "thesis/mesh/TriangleMesh.h"
#include "thesis/shared/Hit.h"
#include "thesis/voxel/Voxelizer.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using namespace thesis;

namespace {

constexpr float kRelativeTolerance = 1e-4f;
// Rounding decides rays that graze an edge or a corner of a voxel. Such rays are rare.
constexpr double kMaxDisagreementFraction = 1e-3;

encoding::EncodingRegistry makeEncodings()
{
    encoding::EncodingRegistry registry;
    plugins::registerBuiltInEncodings(registry);
    return registry;
}

// Deterministic pseudo-random bits (SplitMix64), the same on every platform.
std::uint64_t hashOf(const glm::uvec3& p)
{
    std::uint64_t value = p.x + 4096ull * (p.y + 4096ull * p.z) + 0x9E3779B97F4A7C15ull;
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
    return value ^ (value >> 31);
}

// A packed normal that points mostly along `axis` (0 = x, 1 = y, 2 = z). `variation` (0..255)
// changes it a little, so that voxels can be told apart by their normal. The encoder takes the
// primary axis of a brick from its normals, so this also selects the primary axis.
std::uint16_t normalAlong(std::uint32_t axis, std::uint32_t variation)
{
    const std::uint32_t small = 120 + (variation & 15);        // near the middle value: a component near 0
    const std::uint32_t large = 235 + ((variation >> 4) & 15); // near the top value: a component near 1
    const std::uint32_t other = 120 + ((variation >> 4) & 15);
    const std::uint32_t x = axis == 0 ? large : small;
    const std::uint32_t y = axis == 1 ? large : (axis == 0 ? small : other);
    return static_cast<std::uint16_t>(x | (y << 8));
}

struct NamedModel {
    std::string name;
    voxel::VoxelModel model;
    std::vector<tests::Viewpoint> viewpoints;
};

// Models that together reach every part of the brick test.
std::vector<NamedModel> testModels()
{
    const std::vector<tests::Viewpoint> around{
        {35.0f, 30.0f, 0.7f}, {125.0f, 10.0f, 0.5f}, {300.0f, -20.0f, 0.6f}, {0.0f, 89.0f, 0.6f}};
    std::vector<NamedModel> models;

    // A thin closed surface, as a scanned model has: mostly height cells. The last camera is
    // inside the hollow sphere, so its rays reach the bricks from the inside.
    const mesh::TriangleMesh sphere = mesh::makeSphere({0.0f, 0.0f, 0.0f}, 1.0f, 48);
    std::vector<tests::Viewpoint> aroundAndInside = around;
    aroundAndInside.push_back({200.0f, 15.0f, 0.02f});
    models.push_back({"sphere, sampled, bricks of 8",
                      voxel::voxelize(sphere, {64, 8, voxel::VoxelizationRule::Sampled}), aroundAndInside});

    // A thicker surface: many columns hold a run of several voxels.
    models.push_back({"sphere, conservative, bricks of 16",
                      voxel::voxelize(sphere, {64, 16, voxel::VoxelizationRule::Conservative}), around});

    // Scattered voxels: most columns hold several runs, and rays pass many empty cells before a
    // hit. The eight bricks use all three primary axes.
    models.push_back({"scattered voxels, three primary axes",
                      tests::makeModel(16, {2, 2, 2},
                                       [](const glm::uvec3& p) -> std::optional<std::uint16_t> {
                                           const std::uint64_t h = hashOf(p);
                                           if (h % 6 != 0) {
                                               return std::nullopt;
                                           }
                                           const glm::uvec3 brick = p / 16u;
                                           return normalAlong((brick.x + brick.y + brick.z) % 3,
                                                              static_cast<std::uint32_t>(h >> 20));
                                       }),
                      around});

    // Largest bricks, dense scatter: long columns with up to 16 runs.
    models.push_back({"dense scatter, bricks of 32",
                      tests::makeModel(32, {1, 1, 1},
                                       [](const glm::uvec3& p) -> std::optional<std::uint16_t> {
                                           const std::uint64_t h = hashOf(p);
                                           return h % 3 == 0 ? std::optional<std::uint16_t>(
                                                                   normalAlong(2, static_cast<std::uint32_t>(h >> 20)))
                                                             : std::nullopt;
                                       }),
                      around});
    return models;
}

void checkAgainst(const std::vector<core::Hit>& expected, const std::vector<core::Hit>& actual, bool expectHits)
{
    const tests::HitComparison result = tests::compareHits(expected, actual, kRelativeTolerance);
    INFO("both hit " << result.bothHit << ", both miss " << result.bothMiss << ", hit vs miss " << result.hitVersusMiss
                     << ", distance mismatch " << result.distanceMismatch << ", face mismatch " << result.faceMismatch
                     << ", primitive mismatch " << result.primitiveMismatch << ", attribute mismatch "
                     << result.attributeMismatch << ", max relative error " << result.maxRelativeDistanceError);
    const auto fraction = [&](std::size_t count) {
        return static_cast<double>(count) / static_cast<double>(result.rays);
    };
    if (expectHits) {
        CHECK(result.bothHit > result.rays / 10); // the model is in view
    }
    CHECK(fraction(result.hitVersusMiss + result.distanceMismatch) <= kMaxDisagreementFraction);
    // The same distance but another voxel: a ray through an edge that two voxels share.
    CHECK(fraction(result.faceMismatch + result.primitiveMismatch + result.attributeMismatch) <=
          kMaxDisagreementFraction);
}

} // namespace

// The answer for each ray comes from a test of the ray against the cube of every filled voxel
// (CubeByCubeTracer). Each tracer must return it for every encoding: the same distance, the same
// brick, the same side of the voxel, and the normal of the same voxel.
TEST_CASE("tracers find the voxel that a test of every voxel cube finds", "[gpu][brick]")
{
    const gpu::Context context(tests::testContextOptions("Thesis brick test"));
    const std::uint64_t validationBefore = gpu::validationMessageCount();
    const encoding::EncodingRegistry encodings = makeEncodings();
    const workloads::CameraRays cameraRays(context, THESIS_DEFAULT_SHADER_DIR);
    const tracing::RayBatch batch = tracing::RayBatch::create(context, 128, 96);

    for (const NamedModel& input : testModels()) {
        const tests::CubeByCubeTracer cubeByCube(input.model);
        // The expected hits of each viewpoint, worked out when the first encoding needs them.
        std::vector<std::vector<core::Hit>> expected(input.viewpoints.size());
        std::vector<std::vector<core::Hit>> firstEncoding; // hits of tracer 0, to compare the encodings

        for (const std::string& encodingName : encodings.names()) {
            const core::Scene scene = encodings.create(encodingName)->encode(input.model).scene;
            const tracing::GpuScene gpuScene = tracing::GpuScene::upload(context, scene);
            const tests::HitFindingTracers set = tests::makeHitFindingTracers(context, gpuScene);
            REQUIRE(set.tracers.size() >= 2);

            std::vector<std::vector<core::Hit>> thisEncoding;
            for (std::size_t view = 0; view < input.viewpoints.size(); ++view) {
                const core::CameraFrame camera = tests::cameraFrame(scene.bounds, input.viewpoints[view], 4.0f / 3.0f);
                const std::vector<core::Ray> rays = tests::generateRays(context, cameraRays, camera, batch);
                if (expected[view].empty()) {
                    expected[view] = cubeByCube.trace(rays);
                }
                for (std::size_t t = 0; t < set.tracers.size(); ++t) {
                    INFO(input.name << ", " << encodingName << ", tracer " << set.names[t] << ", viewpoint " << view);
                    const std::vector<core::Hit> hits = tests::traceRays(context, *set.tracers[t], batch);
                    checkAgainst(expected[view], hits, true);
                    if (t == 0) {
                        thisEncoding.push_back(hits);
                    }
                }
            }

            // Every encoding stores the same voxels, so every encoding gives the same hits.
            if (firstEncoding.empty()) {
                firstEncoding = std::move(thisEncoding);
            } else {
                for (std::size_t view = 0; view < input.viewpoints.size(); ++view) {
                    INFO(input.name << ": " << encodingName << " against " << encodings.names().front()
                                    << ", viewpoint " << view);
                    const tests::HitComparison same =
                        tests::compareHits(firstEncoding[view], thisEncoding[view], 1e-6f);
                    CHECK(same.disagreements() + same.primitiveMismatch + same.attributeMismatch == 0);
                }
            }
        }
    }

    CHECK(gpu::validationMessageCount() == validationBefore);
}

// Rays with answers worked out by hand, on one brick of 8 x 8 x 8 voxels of size 1 at the origin.
// Four voxels are filled:
//
//   A  = (2, 3, 4)                      alone
//   B1 = (5, 5, 1), B2 = (5, 5, 2)      two voxels on top of each other,
//   B5 = (5, 5, 5)                      and one more above a gap of two voxels
//
// The box around them goes from (2, 3, 1) to (6, 6, 6). The test runs three times, with the
// primary axis of the brick along x, y and z. With z, voxel A is a height cell and the column of
// B is a pointer cell with two runs. The hits must be the same in all three cases.
//
// Faces: 0..5 = -X, +X, -Y, +Y, -Z, +Z (the side the ray enters through).
TEST_CASE("tracers return hand-computed hits inside a brick", "[gpu][brick][known-answer]")
{
    const gpu::Context context(tests::testContextOptions("Thesis brick known-answer test"));
    const std::uint64_t validationBefore = gpu::validationMessageCount();
    const encoding::EncodingRegistry encodings = makeEncodings();

    enum Voxel : std::uint32_t { A = 0, B1 = 1, B2 = 2, B5 = 3, None = 4 };
    const glm::uvec3 positions[] = {{2, 3, 4}, {5, 5, 1}, {5, 5, 2}, {5, 5, 5}};

    struct Case {
        const char* what;
        core::Ray ray;
        Voxel voxel;
        float t;
        std::uint32_t face;
    };
    constexpr float kFar = 1.0e30f;
    const float s2 = std::sqrt(0.5f);
    const float s17 = std::sqrt(17.0f);
    const Case cases[] = {
        {"+x into the side of A", {{-1.0f, 3.5f, 4.5f}, 0.0f, {1.0f, 0.0f, 0.0f}, kFar}, A, 3.0f, 0},
        {"-z onto the top of A", {{2.5f, 3.5f, 10.0f}, 0.0f, {0.0f, 0.0f, -1.0f}, kFar}, A, 5.0f, 5},
        {"+z onto the bottom of A", {{2.5f, 3.5f, -2.0f}, 0.0f, {0.0f, 0.0f, 1.0f}, kFar}, A, 6.0f, 4},
        {"-z onto the upper run of B", {{5.5f, 5.5f, 10.0f}, 0.0f, {0.0f, 0.0f, -1.0f}, kFar}, B5, 4.0f, 5},
        {"-z, starts in the gap of B (tMin)", {{5.5f, 5.5f, 10.0f}, 5.5f, {0.0f, 0.0f, -1.0f}, kFar}, B2, 7.0f, 5},
        {"+z onto the lower run of B", {{5.5f, 5.5f, -1.0f}, 0.0f, {0.0f, 0.0f, 1.0f}, kFar}, B1, 2.0f, 4},
        {"+x through the gap of B", {{-1.0f, 5.5f, 3.5f}, 0.0f, {1.0f, 0.0f, 0.0f}, kFar}, None, 0.0f, 0},
        {"+x into the side of the lower run", {{-1.0f, 5.5f, 2.5f}, 0.0f, {1.0f, 0.0f, 0.0f}, kFar}, B2, 6.0f, 0},
        {"-y into the side of B5", {{5.5f, 9.0f, 5.5f}, 0.0f, {0.0f, -1.0f, 0.0f}, kFar}, B5, 3.0f, 3},
        {"diagonal onto the top of A", {{-2.0f, 3.5f, 9.5f}, 0.0f, {s2, 0.0f, -s2}, kFar}, A, 4.5f / s2, 5},
        {"diagonal into the side of A", {{-2.0f, 3.5f, 8.5f}, 0.0f, {s2, 0.0f, -s2}, kFar}, A, 4.0f / s2, 0},
        {"flat ray onto the top of A",
         {{-3.5f, 3.5f, 6.5f}, 0.0f, {4.0f / s17, 0.0f, -1.0f / s17}, kFar},
         A,
         1.5f * s17,
         5},
        {"flat ray into the side of A",
         {{-6.0f, 3.5f, 6.5f}, 0.0f, {4.0f / s17, 0.0f, -1.0f / s17}, kFar},
         A,
         2.0f * s17,
         0},
        {"stops short (tMax)", {{-1.0f, 3.5f, 4.5f}, 0.0f, {1.0f, 0.0f, 0.0f}, 2.5f}, None, 0.0f, 0},
        {"starts inside the box, above A", {{2.5f, 3.5f, 5.5f}, 0.0f, {0.0f, 0.0f, -1.0f}, kFar}, A, 0.5f, 5},
        {"starts inside the box, moves away", {{2.5f, 3.5f, 5.5f}, 0.0f, {0.0f, 0.0f, 1.0f}, kFar}, None, 0.0f, 0},
        {"passes above all voxels", {{-1.0f, 3.5f, 7.5f}, 0.0f, {1.0f, 0.0f, 0.0f}, kFar}, None, 0.0f, 0},
    };
    std::vector<core::Ray> rays;
    for (const Case& c : cases) {
        rays.push_back(c.ray);
    }
    const tracing::RayBatch batch = tests::uploadRays(context, rays);

    for (std::uint32_t axis = 0; axis < 3; ++axis) {
        const auto normalOf = [&](std::uint32_t voxel) { return normalAlong(axis, 17 * voxel + 3); };
        const voxel::VoxelModel model = tests::makeModel(
            8, {1, 1, 1},
            [&](const glm::uvec3& p) -> std::optional<std::uint16_t> {
                for (std::uint32_t voxel = 0; voxel < std::size(positions); ++voxel) {
                    if (p == positions[voxel]) {
                        return normalOf(voxel);
                    }
                }
                return std::nullopt;
            },
            {0.0f, 0.0f, 0.0f}, 1.0f);

        for (const std::string& encodingName : encodings.names()) {
            const encoding::EncodedModel encoded = encodings.create(encodingName)->encode(model);
            const char* axisNames[] = {"x", "y", "z"};
            REQUIRE(encoded.statistics.at("bricks_by_primary_axis").at(axisNames[axis]).get<std::uint64_t>() == 1);
            const tracing::GpuScene gpuScene = tracing::GpuScene::upload(context, encoded.scene);
            const tests::HitFindingTracers set = tests::makeHitFindingTracers(context, gpuScene);
            REQUIRE_FALSE(set.tracers.empty());

            for (std::size_t t = 0; t < set.tracers.size(); ++t) {
                const std::vector<core::Hit> hits = tests::traceRays(context, *set.tracers[t], batch);
                for (std::size_t i = 0; i < std::size(cases); ++i) {
                    const Case& expected = cases[i];
                    INFO("primary axis " << axis << ", " << encodingName << ", " << set.names[t] << ": "
                                         << expected.what);
                    if (expected.voxel == None) {
                        CHECK(hits[i].primitive == core::kNoHit);
                        continue;
                    }
                    CHECK(hits[i].primitive == 0);
                    CHECK(hits[i].t == Catch::Approx(expected.t).epsilon(1e-5));
                    CHECK(hits[i].face == expected.face);
                    CHECK(hits[i].attributes == (std::uint32_t{THESIS_HIT_HAS_NORMAL} | normalOf(expected.voxel)));
                }
            }
        }
    }

    CHECK(gpu::validationMessageCount() == validationBefore);
}

// The counting variant of a tracer must find the same hits as the timed variant, and its
// counters must make sense.
TEST_CASE("the counting variant finds the same hits and counts the work", "[gpu][brick][work]")
{
    const gpu::Context context(tests::testContextOptions("Thesis work counter test"));
    const std::uint64_t validationBefore = gpu::validationMessageCount();
    const encoding::EncodingRegistry encodings = makeEncodings();
    const workloads::CameraRays cameraRays(context, THESIS_DEFAULT_SHADER_DIR);
    const tracing::RayBatch batch = tracing::RayBatch::create(context, 128, 96);

    const voxel::VoxelModel model = voxel::voxelize(mesh::makeSphere({0.0f, 0.0f, 0.0f}, 1.0f, 48),
                                                    {64, 16, voxel::VoxelizationRule::Conservative});
    for (const std::string& encodingName : encodings.names()) {
        const core::Scene scene = encodings.create(encodingName)->encode(model).scene;
        const tracing::GpuScene gpuScene = tracing::GpuScene::upload(context, scene);
        const tests::HitFindingTracers set = tests::makeHitFindingTracers(context, gpuScene);
        const core::CameraFrame camera = tests::cameraFrame(scene.bounds, {35.0f, 30.0f, 0.7f}, 4.0f / 3.0f);
        (void)tests::generateRays(context, cameraRays, camera, batch);

        for (std::size_t t = 0; t < set.tracers.size(); ++t) {
            INFO(encodingName << ", tracer " << set.names[t]);
            REQUIRE(set.tracers[t]->countsWork());
            const std::vector<core::Hit> timed = tests::traceRays(context, *set.tracers[t], batch);
            std::vector<core::RayWork> work;
            const std::vector<core::Hit> counted = tests::traceRaysCounting(context, *set.tracers[t], batch, work);

            const tests::HitComparison same = tests::compareHits(timed, counted, 1e-6f);
            CHECK(same.disagreements() + same.primitiveMismatch + same.attributeMismatch == 0);
            CHECK(same.bothHit > same.rays / 10);

            std::size_t wrong = 0;
            std::size_t cellKinds[4] = {};
            for (std::size_t ray = 0; ray < work.size(); ++ray) {
                const bool hit = counted[ray].primitive != core::kNoHit;
                // A hit needs at least one brick test and one cell, and it names the kind of its cell.
                const bool hitMakesSense = work[ray].primitiveTests >= 1 && work[ray].cells >= 1 &&
                                           work[ray].hitCell >= THESIS_HIT_CELL_HEIGHT &&
                                           work[ray].hitCell <= THESIS_HIT_CELL_SEVERAL_RUNS;
                const bool missMakesSense = work[ray].hitCell == std::uint32_t{THESIS_HIT_CELL_NONE};
                // A ray visits cells only inside bricks that it was tested against.
                const bool countsMakeSense =
                    work[ray].cells <= work[ray].primitiveTests * 2 * 16 && work[ray].runs <= work[ray].cells * 8;
                if (!(hit ? hitMakesSense : missMakesSense) || !countsMakeSense) {
                    ++wrong;
                }
                if (work[ray].hitCell <= THESIS_HIT_CELL_SEVERAL_RUNS) {
                    ++cellKinds[work[ray].hitCell];
                }
            }
            CHECK(wrong == 0);
            // The conservative sphere has columns with one voxel and columns with a run.
            CHECK(cellKinds[THESIS_HIT_CELL_HEIGHT] > 0);
            CHECK(cellKinds[THESIS_HIT_CELL_ONE_RUN] > 0);
        }
    }

    CHECK(gpu::validationMessageCount() == validationBefore);
}
