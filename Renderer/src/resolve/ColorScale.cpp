#include "thesis/resolve/ColorScale.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <utility>

namespace thesis::resolve {
namespace {

using Coefficients = std::array<double, 3>;
constexpr std::array<Coefficients, 7> kViridis{{{THESIS_VIRIDIS_C0},
                                                {THESIS_VIRIDIS_C1},
                                                {THESIS_VIRIDIS_C2},
                                                {THESIS_VIRIDIS_C3},
                                                {THESIS_VIRIDIS_C4},
                                                {THESIS_VIRIDIS_C5},
                                                {THESIS_VIRIDIS_C6}}};

// The log scale needs a maximum above 1: log2(1) = 0. A smaller maximum counts as 2, here and in
// shade_work.slang.
constexpr double kSmallestLogMaximum = 2.0;
constexpr std::size_t kMostLogTicks = 7;

} // namespace

DisplayColor viridis(float t)
{
    const double x = std::clamp(static_cast<double>(t), 0.0, 1.0);
    std::array<double, 3> rgb{};
    for (std::size_t channel = 0; channel < 3; ++channel) {
        double value = kViridis[6][channel];
        for (std::size_t k = 6; k-- > 0;) {
            value = kViridis[k][channel] + x * value;
        }
        rgb[channel] = std::clamp(value, 0.0, 1.0);
    }
    return {static_cast<float>(rgb[0]), static_cast<float>(rgb[1]), static_cast<float>(rgb[2])};
}

float ColorScale::position(double count) const
{
    double t = 0.0;
    if (kind == ScaleKind::Log2) {
        t = std::log2(std::max(count, 1.0)) / std::log2(std::max<double>(maximum, kSmallestLogMaximum));
    } else {
        t = count / std::max<double>(maximum, 1.0);
    }
    return static_cast<float>(std::clamp(t, 0.0, 1.0));
}

double ColorScale::countAt(float t) const
{
    const double x = std::clamp(static_cast<double>(t), 0.0, 1.0);
    if (kind == ScaleKind::Log2) {
        return std::exp2(x * std::log2(std::max<double>(maximum, kSmallestLogMaximum)));
    }
    return x * std::max<double>(maximum, 1.0);
}

DisplayColor ColorScale::color(double count) const
{
    if (count <= 0.0) {
        return kZeroCountColor;
    }
    if (count > maximum) {
        return kAboveScaleColor;
    }
    return viridis(position(count));
}

std::vector<ScaleTick> ColorScale::ticks() const
{
    std::vector<double> values;
    if (kind == ScaleKind::Log2) {
        for (double value = 1.0; value <= maximum; value *= 2.0) {
            values.push_back(value);
        }
        if (values.size() > kMostLogTicks) {
            std::vector<double> everySecond;
            for (std::size_t i = 0; i < values.size(); i += 2) {
                everySecond.push_back(values[i]);
            }
            values = std::move(everySecond);
        }
    } else {
        // A step of 1, 2 or 5 times a power of ten, at least maximum / 5, and at least 1: counts are
        // whole numbers.
        const double rough = std::max<double>(maximum, 1.0) / 5.0;
        const double magnitude = std::pow(10.0, std::floor(std::log10(rough)));
        const double normalised = rough / magnitude;
        const double factor = normalised <= 1.0 ? 1.0 : normalised <= 2.0 ? 2.0 : normalised <= 5.0 ? 5.0 : 10.0;
        const double step = std::max(1.0, factor * magnitude);
        for (double value = step; value <= maximum * (1.0 + 1e-9); value += step) {
            values.push_back(value);
        }
    }

    std::vector<ScaleTick> result;
    result.reserve(values.size());
    for (const double value : values) {
        result.push_back({value, position(value), formatCount(value)});
    }
    return result;
}

std::string ColorScale::aboveLabel() const
{
    return "> " + formatCount(maximum);
}

const char* ColorScale::kindName() const
{
    return kind == ScaleKind::Log2 ? "log2" : "linear";
}

std::string formatCount(double value)
{
    char text[32];
    if (std::abs(value - std::round(value)) < 1e-9) {
        std::snprintf(text, sizeof(text), "%.0f", value);
    } else {
        std::snprintf(text, sizeof(text), "%.3g", value);
    }
    return text;
}

} // namespace thesis::resolve
