// The colour scale of the work views and the colour bar of screenshots. No GPU.

#include "thesis/resolve/ColorScale.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdlib>
#include <vector>

using namespace thesis;
using Catch::Approx;

namespace {

std::vector<double> tickValues(const resolve::ColorScale& scale)
{
    std::vector<double> values;
    for (const resolve::ScaleTick& tick : scale.ticks()) {
        values.push_back(tick.value);
    }
    return values;
}

double linearChannel(float display)
{
    return display <= 0.04045f ? display / 12.92 : std::pow((display + 0.055) / 1.055, 2.4);
}

} // namespace

TEST_CASE("the viridis fit stays close to the reference table of matplotlib", "[resolve]")
{
    // Entries 0, 128 and 255 of matplotlib's 256-entry viridis table.
    struct Reference {
        float t;
        resolve::DisplayColor color;
    };
    const Reference references[] = {{0.0f, {0.267004f, 0.004874f, 0.329415f}},
                                    {128.0f / 255.0f, {0.127568f, 0.566949f, 0.550556f}},
                                    {1.0f, {0.993248f, 0.906157f, 0.143936f}}};
    for (const Reference& reference : references) {
        INFO("t = " << reference.t);
        const resolve::DisplayColor color = resolve::viridis(reference.t);
        CHECK(std::abs(color.r - reference.color.r) < 0.015f);
        CHECK(std::abs(color.g - reference.color.g) < 0.015f);
        CHECK(std::abs(color.b - reference.color.b) < 0.015f);
    }
}

TEST_CASE("viridis gets lighter all along the scale", "[resolve]")
{
    // Relative luminance (Rec. 709) must rise with t, so a larger count never looks darker.
    double previous = -1.0;
    for (int i = 0; i <= 64; ++i) {
        const resolve::DisplayColor color = resolve::viridis(static_cast<float>(i) / 64.0f);
        const double luminance =
            0.2126 * linearChannel(color.r) + 0.7152 * linearChannel(color.g) + 0.0722 * linearChannel(color.b);
        INFO("step " << i);
        CHECK(luminance > previous);
        previous = luminance;
    }
}

TEST_CASE("colour scale maps counts as the shader does", "[resolve]")
{
    resolve::ColorScale linear{64.0f, resolve::ScaleKind::Linear};
    CHECK(linear.position(16.0) == Approx(0.25));
    CHECK(linear.position(64.0) == Approx(1.0));
    CHECK(linear.countAt(0.25f) == Approx(16.0));

    resolve::ColorScale log{64.0f, resolve::ScaleKind::Log2};
    CHECK(log.position(1.0) == Approx(0.0));
    CHECK(log.position(8.0) == Approx(0.5));
    CHECK(log.countAt(0.5f) == Approx(8.0));

    // Outside the map: black for 0, white above the maximum.
    CHECK(linear.color(0.0).r == 0.0f);
    CHECK(linear.color(65.0).g == 1.0f);
    CHECK(linear.color(64.0).r == Approx(resolve::viridis(1.0f).r));
}

TEST_CASE("colour scale ticks are round counts inside the map", "[resolve]")
{
    using Values = std::vector<double>;
    CHECK(tickValues({64.0f, resolve::ScaleKind::Linear}) == Values{20, 40, 60});
    CHECK(tickValues({100.0f, resolve::ScaleKind::Linear}) == Values{20, 40, 60, 80, 100});
    CHECK(tickValues({10.0f, resolve::ScaleKind::Linear}) == Values{2, 4, 6, 8, 10});
    CHECK(tickValues({3.0f, resolve::ScaleKind::Linear}) == Values{1, 2, 3});
    CHECK(tickValues({64.0f, resolve::ScaleKind::Log2}) == Values{1, 2, 4, 8, 16, 32, 64});
    CHECK(tickValues({1024.0f, resolve::ScaleKind::Log2}) == Values{1, 4, 16, 64, 256, 1024});
    // A scale as large as a 32-bit count still has at most seven ticks.
    CHECK(tickValues({4.0e9f, resolve::ScaleKind::Log2}).size() <= 7);

    const std::vector<resolve::ScaleTick> ticks = resolve::ColorScale{64.0f, resolve::ScaleKind::Log2}.ticks();
    CHECK(ticks[3].label == "8");
    CHECK(ticks[3].position == Approx(0.5));
    CHECK(resolve::ColorScale{64.0f}.aboveLabel() == "> 64");
    CHECK(resolve::formatCount(2.5) == "2.5");
}

TEST_CASE("round ticks lie on round steps inside the range", "[resolve]")
{
    using Values = std::vector<double>;
    const auto values = [](double start, double end, bool whole) {
        Values result;
        for (const resolve::ScaleTick& tick : resolve::roundTicks(start, end, whole)) {
            result.push_back(tick.value);
        }
        return result;
    };
    CHECK(values(0.0, 64.0, true) == Values{0, 20, 40, 60});
    CHECK(values(3.0, 17.0, false) == Values{5, 10, 15});
    CHECK(values(0.0, 1.0, true) == Values{0, 1});
    CHECK(values(5.0, 5.0, false).empty());

    const std::vector<resolve::ScaleTick> ticks = resolve::roundTicks(0.0, 2.0, false);
    REQUIRE(ticks.size() == 5);
    CHECK(ticks[0].label == "0");
    CHECK(ticks[1].label == "0.5");
    CHECK(ticks[1].position == Approx(0.25));
}
