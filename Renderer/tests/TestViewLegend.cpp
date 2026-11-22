// The legend of each view, and the strip that --screenshot puts below the image. No GPU.

#include "thesis/resolve/LegendImage.h"
#include "thesis/resolve/LegendPainter.h"
#include "thesis/resolve/ViewLegend.h"
#include "thesis/resolve/ViewSupport.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

using namespace thesis;
using Catch::Approx;
using resolve::ShadingMode;

namespace {

resolve::ShadingParams paramsFor(ShadingMode mode)
{
    resolve::ShadingParams params;
    params.mode = mode;
    params.distanceNear = 3.0f;
    params.distanceFar = 17.0f;
    return params;
}

std::vector<std::string> labels(const resolve::ViewLegend& legend)
{
    std::vector<std::string> result;
    for (const resolve::Swatch& swatch : legend.swatches) {
        result.push_back(swatch.label);
    }
    return result;
}

// True when some pixel of the strip below `imageHeight` rows has `color`, within 3 of 255.
bool stripHasColor(const resolve::RgbaImage& result, std::uint32_t imageHeight, resolve::DisplayColor color)
{
    const auto near = [](std::byte value, float expected) {
        return std::abs(static_cast<int>(value) - static_cast<int>(std::lround(expected * 255.0f))) <= 3;
    };
    for (std::size_t i = std::size_t{imageHeight} * result.width * 4; i < result.pixels.size(); i += 4) {
        const std::byte* p = &result.pixels[i];
        if (near(p[0], color.r) && near(p[1], color.g) && near(p[2], color.b)) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("every view whose colours stand for values has a legend", "[resolve]")
{
    for (std::size_t mode = 0; mode < resolve::kShadingModeNames.size(); ++mode) {
        const auto view = static_cast<ShadingMode>(mode);
        INFO(resolve::kShadingModeNames[mode]);
        // The lit views show light; primitive id and stored normal are read as pictures.
        const bool noLegend = view == ShadingMode::Shaded || view == ShadingMode::Faces ||
                              view == ShadingMode::PrimitiveId || view == ShadingMode::Normal;
        const resolve::ViewLegend legend = resolve::legendFor(paramsFor(view));
        CHECK(legend.empty() == noLegend);
        if (!noLegend) {
            // Something besides the title says what the colours mean.
            CHECK((legend.bar || !legend.swatches.empty() || !legend.note.empty()));
        }
    }
}

TEST_CASE("legend colours are display-encoded like the image", "[resolve]")
{
    // toDisplay() in display.slang: value^(1 / 2.2).
    const float half = std::pow(0.5f, 1.0f / 2.2f);
    CHECK(resolve::toDisplay(0.5f, 0.0f, 1.0f).r == Approx(half));
    CHECK(resolve::toDisplay(0.5f, 0.0f, 1.0f).g == 0.0f);

    // A category colour of the shader, (0.15, 0.70, 0.20) for a height cell, as the image stores it.
    const resolve::ViewLegend hitCell = resolve::legendFor(paramsFor(ShadingMode::WorkHitCell));
    REQUIRE(!hitCell.swatches.empty());
    CHECK(hitCell.swatches[0].color.g == Approx(std::pow(0.70f, 1.0f / 2.2f)));
}

TEST_CASE("distance legend spans the range from white to black", "[resolve]")
{
    const resolve::ViewLegend legend = resolve::legendFor(paramsFor(ShadingMode::Distance));
    REQUIRE(legend.bar);
    const resolve::LegendBar& bar = *legend.bar;
    CHECK(bar.ramp.front().r == Approx(1.0f));
    CHECK(bar.ramp.back().r == Approx(0.0f));
    for (const resolve::ScaleTick& tick : bar.ticks) {
        CHECK(tick.value >= 3.0);
        CHECK(tick.value <= 17.0);
    }
    CHECK(bar.valueAt(0.5f) == "10");
    CHECK(labels(legend) == std::vector<std::string>{"No hit"});
}

TEST_CASE("category legends name every colour of their view", "[resolve]")
{
    CHECK(labels(resolve::legendFor(paramsFor(ShadingMode::WorkSkipLevel))) ==
          std::vector<std::string>{"2x2", "4x4", "8x8", "16x16", "32x32", "64x64", "None skipped"});
    CHECK(labels(resolve::legendFor(paramsFor(ShadingMode::WorkHitCell))) ==
          std::vector<std::string>{"Height cell: one voxel", "One run", "Several runs", "Primitive without cells",
                                   "No hit"});
    // A count view: the bar with a box for 0 and one for counts above the maximum.
    const resolve::ViewLegend count = resolve::legendFor(paramsFor(ShadingMode::WorkCells));
    REQUIRE(count.bar);
    CHECK(count.bar->below->label == "0");
    CHECK(count.bar->above->label == "> 64");
    CHECK(count.bar->valueAt(0.25f) == "16");
}

TEST_CASE("legend strip goes below the image with the colours of the legend", "[resolve]")
{
    const resolve::RgbaImage image{960, 540, std::vector<std::byte>(960 * 540 * 4, std::byte{0})};
    for (std::size_t mode = 0; mode < resolve::kShadingModeNames.size(); ++mode) {
        INFO(resolve::kShadingModeNames[mode]);
        const resolve::ViewLegend legend = resolve::legendFor(paramsFor(static_cast<ShadingMode>(mode)));
        const resolve::RgbaImage result = resolve::withLegend(image, legend);
        REQUIRE(result.width == image.width);
        REQUIRE(result.pixels.size() == std::size_t{result.width} * result.height * 4);
        if (legend.empty()) {
            CHECK(result.height == image.height);
            continue;
        }
        REQUIRE(result.height > image.height);
        CHECK(std::equal(image.pixels.begin(), image.pixels.end(), result.pixels.begin()));
        for (const resolve::Swatch& swatch : legend.swatches) {
            INFO(swatch.label);
            CHECK(stripHasColor(result, image.height, swatch.color));
        }
        if (legend.bar) {
            CHECK(stripHasColor(result, image.height, legend.bar->ramp[legend.bar->ramp.size() / 2]));
        }
    }
}

// A painter that only records where text goes: 16 px font, 8 px for each character.
class TextRecorder final : public resolve::ILegendPainter {
public:
    std::vector<float> textLeft;
    float fontSize() const override { return 16.0f; }
    float textWidth(const std::string& text) const override { return 8.0f * static_cast<float>(text.size()); }
    void fillVertical(float, float, float, float, resolve::DisplayColor, resolve::DisplayColor) override {}
    void fillHorizontal(float, float, float, float, resolve::DisplayColor, resolve::DisplayColor) override {}
    void outline(float, float, float, float, resolve::DisplayColor) override {}
    void text(float x, float, const std::string&, resolve::DisplayColor) override { textLeft.push_back(x); }
};

// A docked image can leave the legend a column narrower than its end boxes and tick labels.
TEST_CASE("a legend in a narrow column keeps its strip and labels in order", "[resolve]")
{
    resolve::ShadingParams params = paramsFor(ShadingMode::WorkCycles);
    params.workScale.maximum = 20000.0f;
    const resolve::ViewLegend legend = resolve::legendFor(params);
    for (const float width : {400.0f, 60.0f, 8.0f, 0.0f}) {
        INFO("column " << width << " px");
        TextRecorder painter;
        const resolve::LegendLayout layout = resolve::paintLegend(painter, legend, 100.0f, 0.0f, width);
        REQUIRE(layout.hasRamp);
        CHECK(layout.rampRight >= layout.rampLeft);
        CHECK(layout.height > 0.0f);
    }
}

// The window and --screenshot refuse the same views (resolve::unavailableView).
TEST_CASE("a view is refused when the tracer or the scene lacks what it needs", "[resolve]")
{
    const resolve::ViewSupport all{true, true, true, true, true};
    for (std::size_t i = 0; i < resolve::kShadingModeNames.size(); ++i) {
        const auto mode = static_cast<ShadingMode>(i);
        INFO(resolve::kShadingModeNames[i]);
        CHECK(resolve::unavailableView(mode, all) == nullptr);

        resolve::ViewSupport noWork = all;
        noWork.countsWork = false;
        CHECK((resolve::unavailableView(mode, noWork) != nullptr) == resolve::showsWork(mode));

        resolve::ViewSupport noCycles = all;
        noCycles.countsCycles = false;
        CHECK((resolve::unavailableView(mode, noCycles) != nullptr) == (mode == ShadingMode::WorkCycles));

        resolve::ViewSupport noSteps = all;
        noSteps.countsStructureSteps = false;
        CHECK((resolve::unavailableView(mode, noSteps) != nullptr) == (mode == ShadingMode::WorkStructure));

        resolve::ViewSupport boxField = all;
        boxField.voxelModel = false;
        CHECK((resolve::unavailableView(mode, boxField) != nullptr) ==
              (mode == ShadingMode::Normal || mode == ShadingMode::Faces || mode == ShadingMode::WorkCells ||
               mode == ShadingMode::WorkRuns || mode == ShadingMode::WorkHitCell));

        resolve::ViewSupport noMipmap = all;
        noMipmap.skipsEmptySpace = false;
        CHECK((resolve::unavailableView(mode, noMipmap) != nullptr) ==
              (mode == ShadingMode::WorkMipmap || mode == ShadingMode::WorkSkipLevel));
    }
}
