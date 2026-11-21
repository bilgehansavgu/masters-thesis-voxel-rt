// Tests that need no GPU.

#include "HitComparison.h"

#include "thesis/core/BoxField.h"
#include "thesis/core/Camera.h"
#include "thesis/core/Registry.h"
#include "thesis/core/Statistics.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/geometric.hpp>

#include <cstring>
#include <memory>
#include <vector>

using namespace thesis;

TEST_CASE("median and summary", "[core]")
{
    const std::vector<double> odd{5.0, 1.0, 3.0};
    const std::vector<double> even{4.0, 1.0, 3.0, 2.0};
    CHECK(core::median(odd) == 3.0);
    CHECK(core::median(even) == 2.5);
    CHECK(core::median(std::vector<double>{}) == 0.0);

    const core::Summary summary = core::summarize(even);
    CHECK(summary.count == 4);
    CHECK(summary.min == 1.0);
    CHECK(summary.max == 4.0);
    CHECK(summary.mean == Catch::Approx(2.5));
    CHECK(summary.standardDeviation == Catch::Approx(1.2909944));
}

TEST_CASE("registry creates by name and rejects duplicates", "[core]")
{
    struct Shape {
        virtual ~Shape() = default;
        virtual int sides() const = 0;
    };
    struct Square : Shape {
        int sides() const override { return 4; }
    };

    core::Registry<Shape> registry;
    registry.add("square", "four sides", [] { return std::make_unique<Square>(); });
    CHECK(registry.contains("square"));
    CHECK_FALSE(registry.contains("circle"));
    CHECK(registry.create("square")->sides() == 4);
    CHECK_THROWS(registry.create("circle"));
    CHECK_THROWS(registry.add("square", "again", [] { return std::make_unique<Square>(); }));
}

TEST_CASE("registry names its default, or else the first plugin", "[core]")
{
    struct Shape {
        virtual ~Shape() = default;
    };
    core::Registry<Shape> registry;
    CHECK_THROWS(registry.defaultName());
    registry.add("square", "", [] { return std::make_unique<Shape>(); });
    CHECK(registry.defaultName() == "square");
    registry.add("circle", "", [] { return std::make_unique<Shape>(); }, core::Pick::Default);
    CHECK(registry.defaultName() == "circle");
    CHECK_THROWS(registry.add("line", "", [] { return std::make_unique<Shape>(); }, core::Pick::Default));
}

TEST_CASE("box field is deterministic and boxes do not overlap", "[core]")
{
    const core::Scene a = core::makeBoxField({});
    const core::Scene b = core::makeBoxField({});
    REQUIRE(a.primitiveBounds.size() == b.primitiveBounds.size());
    CHECK(std::memcmp(a.primitiveBounds.data(), b.primitiveBounds.data(),
                      a.primitiveBounds.size() * sizeof(core::Aabb)) == 0);
    CHECK(a.primitiveKind == "box");
    CHECK(a.primitiveBounds.size() > 300);

    const core::Scene other = core::makeBoxField({.seed = 2});
    CHECK(std::memcmp(a.primitiveBounds.data(), other.primitiveBounds.data(), sizeof(core::Aabb) * 8) != 0);

    // Apart from the ground (index 0), no two boxes share any volume or face.
    const auto separated = [](const core::Aabb& p, const core::Aabb& q) {
        return p.hi.x < q.lo.x || q.hi.x < p.lo.x || p.hi.y < q.lo.y || q.hi.y < p.lo.y || p.hi.z < q.lo.z ||
               q.hi.z < p.lo.z;
    };
    for (std::size_t i = 1; i < a.primitiveBounds.size(); ++i) {
        for (std::size_t j = i + 1; j < a.primitiveBounds.size(); ++j) {
            REQUIRE(separated(a.primitiveBounds[i], a.primitiveBounds[j]));
        }
    }
}

TEST_CASE("orbit camera looks at its target with an orthogonal frame", "[core]")
{
    core::OrbitCamera camera;
    camera.target = {1.0f, 2.0f, 3.0f};
    const core::CameraFrame frame = camera.frame(16.0f / 9.0f);
    CHECK(glm::length(frame.forward) == Catch::Approx(1.0f));
    CHECK(glm::dot(frame.forward, frame.right) == Catch::Approx(0.0f).margin(1e-6));
    CHECK(glm::dot(frame.forward, frame.up) == Catch::Approx(0.0f).margin(1e-6));
    CHECK(glm::length(frame.right) / glm::length(frame.up) == Catch::Approx(16.0f / 9.0f));
    const glm::vec3 toTarget = glm::normalize(camera.target - frame.origin);
    CHECK(glm::dot(toTarget, frame.forward) == Catch::Approx(1.0f));
}

TEST_CASE("flying camera turns in place and moves along its view", "[core]")
{
    core::OrbitCamera camera;
    camera.target = {1.0f, 2.0f, 3.0f};
    const glm::vec3 eyeBefore = camera.eye();

    camera.turnInPlace(40.0f, -25.0f);
    CHECK(glm::length(camera.eye() - eyeBefore) == Catch::Approx(0.0f).margin(1e-4));
    CHECK(glm::length(camera.target - camera.eye()) == Catch::Approx(camera.distance));

    camera.turnInPlace(0.0f, 500.0f);
    CHECK(camera.pitchDegrees == Catch::Approx(89.0f));

    const core::CameraFrame before = camera.frame(1.0f);
    camera.fly(2.0f, 0.0f, 0.0f);
    const glm::vec3 moved = camera.frame(1.0f).origin - before.origin;
    CHECK(glm::length(moved) == Catch::Approx(2.0f));
    CHECK(glm::dot(glm::normalize(moved), before.forward) == Catch::Approx(1.0f));

    const glm::vec3 eyeBeforeSide = camera.eye();
    camera.fly(0.0f, 1.0f, 3.0f);
    const glm::vec3 side = camera.eye() - eyeBeforeSide;
    CHECK(side.y == Catch::Approx(3.0f));
    CHECK(glm::dot(side, before.forward) == Catch::Approx(3.0f * before.forward.y).margin(1e-4));
}

TEST_CASE("hit comparison classifies disagreements", "[core]")
{
    using core::Hit;
    using core::kNoHit;
    const std::vector<Hit> a{{1.0f, 0, 0, 0}, {5.0f, kNoHit, 0, 0}, {2.0f, 1, 0, 0},
                             {3.0f, 2, 0, 0}, {4.0f, 3, 0, 0},      {6.0f, 4, 2, 0}};
    const std::vector<Hit> b{{1.0f, 0, 0, 0}, {5.0f, kNoHit, 0, 0}, {2.0f, 7, 0, 0},
                             {3.5f, 2, 0, 0}, {4.0f, kNoHit, 0, 0}, {6.0f, 4, 3, 0}};
    const tests::HitComparison result = tests::compareHits(a, b, 1e-4f);
    CHECK(result.rays == 6);
    CHECK(result.bothMiss == 1);
    CHECK(result.bothHit == 4);
    CHECK(result.primitiveMismatch == 1);
    CHECK(result.distanceMismatch == 1);
    CHECK(result.faceMismatch == 1);
    CHECK(result.hitVersusMiss == 1);
    CHECK(result.disagreements() == 3);
}
