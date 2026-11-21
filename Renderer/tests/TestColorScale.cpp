// The colour scale of the work views and the colour bar of screenshots. No GPU.

#include "thesis/resolve/ColorBarImage.h"
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

    const std::vector<resolve::ScaleTick> ticks = resolve::ColorScale{64.0f, resolve::ScaleKind::Log2}.ticks();
    CHECK(ticks[3].label == "8");
    CHECK(ticks[3].position == Approx(0.5));
    CHECK(resolve::ColorScale{64.0f}.aboveLabel() == "> 64");
    CHECK(resolve::formatCount(2.5) == "2.5");
}

TEST_CASE("colour bar strip goes below the image with the colours of the scale", "[resolve]")
{
    resolve::RgbaImage image{640, 360, std::vector<std::byte>(640 * 360 * 4, std::byte{0})};
    const resolve::RgbaImage result = resolve::withColorBar(image, {64.0f}, "Cells visited per ray");

    REQUIRE(result.width == image.width);
    REQUIRE(result.height > image.height);
    REQUIRE(result.pixels.size() == std::size_t{result.width} * result.height * 4);
    for (std::size_t i = 0; i < image.pixels.size(); ++i) {
        REQUIRE(result.pixels[i] == image.pixels[i]);
    }

    // Somewhere in the strip: the white box, and the middle colour of the map.
    const resolve::DisplayColor middle = resolve::viridis(0.5f);
    const auto near = [](std::byte value, float expected) {
        return std::abs(static_cast<int>(value) - static_cast<int>(std::lround(expected * 255.0f))) <= 3;
    };
    bool white = false;
    bool middleColor = false;
    for (std::size_t i = image.pixels.size(); i < result.pixels.size(); i += 4) {
        const std::byte* p = &result.pixels[i];
        white = white || (p[0] == std::byte{255} && p[1] == std::byte{255} && p[2] == std::byte{255});
        middleColor = middleColor || (near(p[0], middle.r) && near(p[1], middle.g) && near(p[2], middle.b));
    }
    CHECK(white);
    CHECK(middleColor);
}
