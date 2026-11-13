#include "thesis/core/BoxField.h"

#include <stdexcept>
#include <string>

namespace thesis::core {
namespace {

// SplitMix64: tiny, fast and identical on every platform.
class Random {
public:
    explicit Random(std::uint64_t seed) : state_(seed) {}

    std::uint64_t next()
    {
        std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    // Uniform in [0, 1) with 24 bits of precision.
    float uniform() { return static_cast<float>(next() >> 40) * (1.0f / 16777216.0f); }
    float uniform(float lo, float hi) { return lo + (hi - lo) * uniform(); }

private:
    std::uint64_t state_;
};

Aabb makeBox(const glm::vec3& lo, const glm::vec3& hi)
{
    return Aabb{lo, hi};
}

} // namespace

Scene makeBoxField(const BoxFieldOptions& options)
{
    if (options.gridSize == 0 || options.cellSize <= 0.0f) {
        throw std::invalid_argument("makeBoxField: gridSize and cellSize must be positive");
    }

    Scene scene;
    scene.name = "box-field-" + std::to_string(options.gridSize) + "x" + std::to_string(options.gridSize) + "-seed" +
                 std::to_string(options.seed);
    scene.primitiveKind = "box";

    Random random(options.seed);
    const float cell = options.cellSize;
    const float half = 0.5f * static_cast<float>(options.gridSize) * cell;

    // Ground slab; its top face is y = 0.
    scene.primitiveBounds.push_back(
        makeBox({-half - cell, -0.25f * cell, -half - cell}, {half + cell, 0.0f, half + cell}));

    for (std::uint32_t z = 0; z < options.gridSize; ++z) {
        for (std::uint32_t x = 0; x < options.gridSize; ++x) {
            if (random.uniform() < 0.15f) {
                continue; // leave some cells empty
            }
            const float x0 = -half + static_cast<float>(x) * cell;
            const float z0 = -half + static_cast<float>(z) * cell;
            const float inset = cell * random.uniform(0.08f, 0.30f); // keeps neighbours apart
            const float u = random.uniform();
            const float height = cell * (0.2f + 3.8f * u * u * u); // mostly low, a few towers

            scene.primitiveBounds.push_back(
                makeBox({x0 + inset, 0.0f, z0 + inset}, {x0 + cell - inset, height, z0 + cell - inset}));

            if (random.uniform() < 0.12f) {
                const float size = 0.3f * cell;
                const float cx = x0 + 0.5f * cell;
                const float cz = z0 + 0.5f * cell;
                const float y = height + cell * random.uniform(0.4f, 1.5f);
                scene.primitiveBounds.push_back(
                    makeBox({cx - 0.5f * size, y, cz - 0.5f * size}, {cx + 0.5f * size, y + size, cz + 0.5f * size}));
            }
        }
    }

    for (const Aabb& box : scene.primitiveBounds) {
        scene.bounds.extend(box);
    }
    return scene;
}

} // namespace thesis::core
