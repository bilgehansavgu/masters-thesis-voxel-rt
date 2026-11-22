// The statistics of the work counters (metrics::RayWorkSummary): histogram bins, percentiles,
// the costliest ray and the warp use, against counts worked out by hand. And, on the GPU, the
// grouping of rays into warps that the warp statistics assume.

#include "GpuTestHelpers.h"

#include "thesis/gpu/Buffer.h"
#include "thesis/gpu/ComputeKernel.h"
#include "thesis/gpu/Context.h"
#include "thesis/metrics/RayWorkSummary.h"
#include "thesis/tracing/TraceKernels.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <vector>

using thesis::core::Hit;
using thesis::core::RayWork;
using thesis::metrics::RayWorkSummary;
using thesis::metrics::WorkCounter;

namespace {

RayWork steps(std::uint32_t count)
{
    RayWork work{};
    work.structureSteps = count;
    return work;
}

Hit miss()
{
    return {1.0f, thesis::core::kNoHit, 0, 0};
}

Hit hit()
{
    return {1.0f, 0, 0, 0};
}

} // namespace

TEST_CASE("Work histogram bins hold every count and start where the previous one ends", "[metrics]")
{
    // Exact bins up to 63, then 32 bins for each power of two.
    CHECK(RayWorkSummary::binOf(0) == 0);
    CHECK(RayWorkSummary::binOf(63) == 63);
    CHECK(RayWorkSummary::binOf(64) == 64);
    CHECK(RayWorkSummary::binOf(65) == 64); // 64 and 65 share the bin of width 2
    CHECK(RayWorkSummary::binOf(66) == 65);
    CHECK(RayWorkSummary::binOf(0xFFFFFFFFu) == RayWorkSummary::kBins - 1);
    for (std::size_t bin = 1; bin < RayWorkSummary::kBins; ++bin) {
        INFO("bin " << bin);
        REQUIRE(RayWorkSummary::binStart(bin) > RayWorkSummary::binStart(bin - 1));
        REQUIRE(RayWorkSummary::binOf(static_cast<std::uint32_t>(RayWorkSummary::binStart(bin))) == bin);
        REQUIRE(RayWorkSummary::binOf(static_cast<std::uint32_t>(RayWorkSummary::binStart(bin) - 1)) == bin - 1);
    }
}

TEST_CASE("Work summary gives percentiles, histogram and costliest ray", "[metrics]")
{
    // 100 rays: ray i took i steps (0 .. 99); the even rays hit.
    std::vector<RayWork> work;
    std::vector<Hit> hits;
    for (std::uint32_t i = 0; i < 100; ++i) {
        work.push_back(steps(i));
        hits.push_back(i % 2 == 0 ? hit() : miss());
    }
    RayWorkSummary summary;
    summary.add(work, hits);

    CHECK(summary.rays() == 100);
    CHECK(summary.raysWithHit() == 50);
    CHECK(summary.meanPerRay().structureSteps == Catch::Approx(49.5));
    CHECK(summary.meanPerRayWithHit().structureSteps == Catch::Approx(49.0)); // 0, 2, ..., 98
    CHECK(summary.costliestRay(WorkCounter::StructureSteps) == 99);

    // Of the 99 rays with work (1 .. 99), 50 % stay at or below 50; 99 % (98.01 -> 99 rays) at 99.
    CHECK(summary.percentile(WorkCounter::StructureSteps, 0.5) == Catch::Approx(50.0));  // exact below 64
    CHECK(summary.percentile(WorkCounter::StructureSteps, 0.99) == Catch::Approx(99.0)); // bin [98, 99]
    CHECK(summary.percentile(WorkCounter::Cells, 0.99) == 0.0);                          // no ray visited a cell

    // Octaves: 0 | 1 | 2-3 | 4-7 | 8-15 | 16-31 | 32-63 | 64-127.
    const auto withHit = summary.octaves(WorkCounter::StructureSteps, true);
    const auto without = summary.octaves(WorkCounter::StructureSteps, false);
    CHECK(withHit[0] == 1);  // ray 0
    CHECK(without[1] == 1);  // ray 1
    CHECK(withHit[2] == 1);  // ray 2
    CHECK(without[2] == 1);  // ray 3
    CHECK(withHit[7] == 18); // 64 .. 98, even
    CHECK(without[7] == 18); // 65 .. 99, odd

    CHECK_FALSE(summary.warpUse(WorkCounter::StructureSteps).has_value()); // no warps given
}

TEST_CASE("A percentile in the top bin is the largest count, not the start of the bin", "[metrics]")
{
    RayWorkSummary summary;
    summary.add(std::vector<RayWork>{steps(0xFFFFFFF0u)}, std::vector<Hit>{hit()});
    CHECK(summary.percentile(WorkCounter::StructureSteps, 0.5) >= 0xFFFFFFF0u);
}

TEST_CASE("Frames with different warp sizes do not mix in one summary", "[metrics]")
{
    const std::vector<RayWork> work(8, steps(1));
    const std::vector<Hit> hits(8, hit());
    RayWorkSummary summary;
    summary.add(work, hits, 4);
    CHECK_NOTHROW(summary.add(work, hits, 4));
    CHECK_THROWS_AS(summary.add(work, hits, 0), std::logic_error);
}

TEST_CASE("Warp use is not reported for the clock cycles", "[metrics]")
{
    RayWork a{};
    a.cycles = 100;
    RayWork b{};
    b.cycles = 300;
    RayWorkSummary summary;
    summary.add(std::vector<RayWork>{a, b}, std::vector<Hit>(2, hit()), 2);
    CHECK_FALSE(summary.warpUse(WorkCounter::Cycles).has_value());
}

TEST_CASE("Warp use is the work of the rays over the work their warps were busy for", "[metrics]")
{
    // Two warps of 4 rays. Warp 1: 1, 1, 1, 5 -> busy 4 x 5 = 20 for 8 of work.
    // Warp 2: 2, 2, 2, 2 -> busy 8 for 8. Together 16 / 28.
    const std::vector<RayWork> work{steps(1), steps(1), steps(1), steps(5), steps(2), steps(2), steps(2), steps(2)};
    const std::vector<Hit> hits(work.size(), hit());
    RayWorkSummary summary;
    summary.add(work, hits, 4);
    REQUIRE(summary.warpUse(WorkCounter::StructureSteps).has_value());
    CHECK(*summary.warpUse(WorkCounter::StructureSteps) == Catch::Approx(16.0 / 28.0));

    // A last warp that is not full counts only its own rays: 3 rays of 4 steps -> 12 for 12.
    const std::vector<RayWork> short_{steps(4), steps(4), steps(4)};
    RayWorkSummary partial;
    partial.add(short_, std::vector<Hit>(3, hit()), 4);
    CHECK(*partial.warpUse(WorkCounter::StructureSteps) == Catch::Approx(1.0));
}

TEST_CASE("A counter the tracer does not count is null in every group of the results file", "[metrics]")
{
    const std::vector<RayWork> work{steps(3), steps(5), steps(7), steps(9)};
    RayWorkSummary summary;
    summary.add(work, std::vector<Hit>(work.size(), hit()), 4);

    const thesis::metrics::WorkCounter unavailable[] = {WorkCounter::StructureSteps};
    const nlohmann::json json = summary.toJson(unavailable);
    std::size_t groups = 0;
    for (const nlohmann::json& group : json) {
        if (group.is_object() && group.contains("structure_steps")) {
            CHECK(group["structure_steps"].is_null());
            ++groups;
        }
    }
    // Means of all rays and of rays with a hit, maximum, percentiles, warp use.
    CHECK(groups == 5);
    CHECK(json["mean_per_ray"]["primitive_tests"].is_number()); // other counters stay
    CHECK(summary.toJson()["percentiles_of_rays_with_work"]["structure_steps"]["p50"].is_number());

    // The note on how to read the cycles goes only with cycles.
    CHECK(json.contains("cycles_note"));
    const thesis::metrics::WorkCounter noCycles[] = {WorkCounter::Cycles};
    CHECK_FALSE(summary.toJson(noCycles).contains("cycles_note"));
}

// RayWorkSummary puts rays S * k to S * k + S - 1 of the ray list into one warp, S being the
// subgroup size the device reports (gpu::DeviceInfo::subgroupSize). Vulkan does not promise
// that mapping of compute threads to subgroup lanes, so this test shows it on the GPU at hand,
// in a dispatch shaped like every compute shader tracer's (tracing::kTraceGroupSize).
TEST_CASE("a tracer-shaped dispatch puts consecutive threads into full warps", "[gpu][work]")
{
    using namespace thesis;
    const gpu::Context context(tests::testContextOptions("Thesis warp lane test"));
    const std::uint64_t validationBefore = gpu::validationMessageCount();

    struct WarpLane {
        std::uint32_t laneCount;
        std::uint32_t lane;
        std::uint32_t firstThread;
        std::uint32_t lastThread;
    };
    static_assert(sizeof(WarpLane) == 16); // the struct of tests/shaders/warp_lanes.slang
    constexpr std::uint32_t kThreads = 64 * 1024;
    const gpu::ComputeKernel kernel(
        context, gpu::KernelDescription{std::filesystem::path(THESIS_DEFAULT_SHADER_DIR) / "warp_lanes.spv",
                                        {{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}},
                                        0});
    const gpu::Buffer output(context, kThreads * sizeof(WarpLane), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                             gpu::MemoryUsage::DeviceLocal);
    context.submitAndWait([&](VkCommandBuffer cmd) {
        gpu::KernelResources resources;
        resources.buffer(0, output);
        kernel.dispatch(cmd, resources, gpu::groupCount(kThreads, tracing::kTraceGroupSize), 1, 1);
    });
    const std::vector<std::byte> bytes = gpu::downloadBuffer(context, output);
    std::vector<WarpLane> lanes(kThreads);
    std::memcpy(lanes.data(), bytes.data(), kThreads * sizeof(WarpLane));

    const std::uint32_t size = context.info().subgroupSize;
    REQUIRE(size > 0);
    REQUIRE(tracing::kTraceGroupSize % size == 0); // a work group holds whole warps
    std::size_t wrong = 0;
    for (std::uint32_t thread = 0; thread < kThreads; ++thread) {
        const WarpLane& lane = lanes[thread];
        const std::uint32_t first = thread / size * size;
        if (lane.laneCount != size || lane.lane != thread % size || lane.firstThread != first ||
            lane.lastThread != first + size - 1)
            ++wrong;
    }
    INFO("subgroup size " << size << ", thread 0 in a warp of " << lanes[0].laneCount << " threads "
                          << lanes[0].firstThread << " to " << lanes[0].lastThread);
    CHECK(wrong == 0);

    CHECK(gpu::validationMessageCount() == validationBefore);
}
