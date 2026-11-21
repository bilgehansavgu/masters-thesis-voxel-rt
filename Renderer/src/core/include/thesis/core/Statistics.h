#pragma once

// Side: CPU

#include <cstddef>
#include <span>

namespace thesis::core {

struct Summary {
    std::size_t count = 0;
    double median = 0.0;
    double mean = 0.0;
    double min = 0.0;
    double max = 0.0;
    double standardDeviation = 0.0; // sample standard deviation (n - 1)
};

// Median of `values`; 0 for an empty span. For an even count, the mean of the middle two.
[[nodiscard]] double median(std::span<const double> values);

[[nodiscard]] Summary summarize(std::span<const double> values);

} // namespace thesis::core
