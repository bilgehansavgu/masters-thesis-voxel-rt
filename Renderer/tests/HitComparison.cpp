#include "HitComparison.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace thesis::tests {

HitComparison compareHits(std::span<const core::Hit> a, std::span<const core::Hit> b, float relativeTolerance)
{
    if (a.size() != b.size()) {
        throw std::invalid_argument("compareHits: hit lists have different sizes");
    }
    HitComparison result;
    result.rays = a.size();
    for (std::size_t i = 0; i < a.size(); ++i) {
        const bool hitA = a[i].primitive != core::kNoHit;
        const bool hitB = b[i].primitive != core::kNoHit;
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
        }
    }
    return result;
}

void writeHitImage(const std::filesystem::path& path, std::span<const core::Hit> hits, std::uint32_t width,
                   std::uint32_t height)
{
    float farthest = 0.0f;
    for (const core::Hit& hit : hits) {
        if (hit.primitive != core::kNoHit) {
            farthest = std::max(farthest, hit.t);
        }
    }
    std::ofstream file(path, std::ios::binary);
    file << "P6\n" << width << ' ' << height << "\n255\n";
    for (const core::Hit& hit : hits) {
        unsigned char grey = 0;
        if (hit.primitive != core::kNoHit && farthest > 0.0f) {
            grey = static_cast<unsigned char>(255.0f - 200.0f * std::clamp(hit.t / farthest, 0.0f, 1.0f));
        }
        const unsigned char pixel[3] = {grey, grey, grey};
        file.write(reinterpret_cast<const char*>(pixel), 3);
    }
}

} // namespace thesis::tests
