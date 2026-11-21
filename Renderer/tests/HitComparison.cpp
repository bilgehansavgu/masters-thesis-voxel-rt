#include "HitComparison.h"

#include <algorithm>
#include <fstream>

namespace thesis::tests {

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
