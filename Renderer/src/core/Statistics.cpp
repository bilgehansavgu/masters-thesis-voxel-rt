#include "thesis/core/Statistics.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace thesis::core {

double median(std::span<const double> values)
{
    if (values.empty()) {
        return 0.0;
    }
    std::vector<double> sorted(values.begin(), values.end());
    std::sort(sorted.begin(), sorted.end());
    const std::size_t middle = sorted.size() / 2;
    return (sorted.size() % 2 == 1) ? sorted[middle] : 0.5 * (sorted[middle - 1] + sorted[middle]);
}

Summary summarize(std::span<const double> values)
{
    Summary summary;
    summary.count = values.size();
    if (values.empty()) {
        return summary;
    }
    const auto [minIt, maxIt] = std::minmax_element(values.begin(), values.end());
    summary.min = *minIt;
    summary.max = *maxIt;
    summary.median = median(values);
    summary.mean = std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
    if (values.size() > 1) {
        double sumSquares = 0.0;
        for (double value : values) {
            sumSquares += (value - summary.mean) * (value - summary.mean);
        }
        summary.standardDeviation = std::sqrt(sumSquares / static_cast<double>(values.size() - 1));
    }
    return summary;
}

} // namespace thesis::core
