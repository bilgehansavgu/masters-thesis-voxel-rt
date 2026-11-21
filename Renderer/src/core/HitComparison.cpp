#include "thesis/core/HitComparison.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace thesis::core {

HitComparison compareHits(std::span<const Hit> a, std::span<const Hit> b, float relativeTolerance)
{
    if (a.size() != b.size()) {
        throw std::invalid_argument("compareHits: hit lists have different sizes");
    }
    HitComparison result;
    result.rays = a.size();
    for (std::size_t i = 0; i < a.size(); ++i) {
        const bool hitA = a[i].primitive != kNoHit;
        const bool hitB = b[i].primitive != kNoHit;
        if (!hitA && !hitB) {
            ++result.bothMiss;
            continue;
        }
        if (hitA != hitB) {
            ++result.hitVersusMiss;
            continue;
        }
        ++result.bothHit;
        const float scale = std::max({1.0f, std::abs(a[i].t), std::abs(b[i].t)});
        const float error = std::abs(a[i].t - b[i].t) / scale;
        result.maxRelativeDistanceError = std::max(result.maxRelativeDistanceError, error);
        if (error > relativeTolerance) {
            ++result.distanceMismatch;
        } else if (a[i].primitive != b[i].primitive) {
            ++result.primitiveMismatch;
        } else if (a[i].face != b[i].face) {
            ++result.faceMismatch;
        } else if (a[i].attributes != b[i].attributes) {
            ++result.attributeMismatch;
        }
    }
    return result;
}

} // namespace thesis::core
