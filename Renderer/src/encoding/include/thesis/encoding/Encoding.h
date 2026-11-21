#pragma once

// Side: CPU

#include "thesis/core/Registry.h"
#include "thesis/core/Scene.h"
#include "thesis/voxel/VoxelModel.h"

#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace thesis::encoding {

// Where one brick sits: its coordinate in the brick grid (core::Scene::primitiveGrid), and the
// axis along which the encoding stores its columns (0 = x, 1 = y, 2 = z).
struct BrickPlace {
    glm::uvec3 coordinate{0};
    int columnAxis = 1;
};

// One design choice of an encoding, for people: the name of the setting and its value here.
struct EncodingSetting {
    std::string name;
    std::string value;
    bool operator==(const EncodingSetting&) const = default;
};

// What an encoding makes of a voxel model.
struct EncodedModel {
    // One primitive per brick. `primitiveBounds` are the boxes around the filled voxels of each
    // brick, `primitiveData` are the buffers of the encoding, and `primitiveKind` is the name of
    // the encoding. Ready for tracing::GpuScene::upload.
    core::Scene scene;
    // Numbers that describe the encoded data, e.g. how many cells are of each kind.
    nlohmann::json statistics;
    // The brick of each primitive, in primitive order. For the window, which shows one brick of a ray.
    std::vector<BrickPlace> bricks;
};

// THE ENCODING SOCKET. An encoding turns a voxel model into the buffers that the GPU reads, and
// back. The 2012 brick format is the first family of encodings; a baseline format is added the
// same way. New encodings are plugins: see src/encodings/ and cmake/ThesisPlugins.cmake.
//
// The name of an encoding is also its primitive kind: shaders/primitives/<name>/primitive.slang
// is the shader code that reads its buffers.
//
// The rule every encoding must keep: sameVoxels(decode(encode(model).scene), model).
class IEncoding {
public:
    virtual ~IEncoding() = default;

    [[nodiscard]] virtual std::string_view name() const = 0;
    // Throws std::runtime_error if the model cannot be stored in this encoding.
    [[nodiscard]] virtual EncodedModel encode(const voxel::VoxelModel& model) const = 0;
    // Rebuilds the voxels from the buffers alone. Name and source of the model are not restored.
    [[nodiscard]] virtual voxel::VoxelModel decode(const core::Scene& scene) const = 0;
    // Extra numbers for reports that are too slow to compute on every encode(), e.g. what a
    // different design choice would have cost. Null if the encoding has none.
    [[nodiscard]] virtual nlohmann::json analyze(const voxel::VoxelModel& /*model*/) const { return nullptr; }

    // True if the brick test skips empty space inside a brick (the min/max mipmap of the 2012
    // format).
    [[nodiscard]] virtual bool skipsEmptySpace() const { return false; }
    // The encoding that stores the same bricks with skipping switched the other way, or empty if
    // there is none. The window and the benchmark compare the two ("skipping off / on").
    [[nodiscard]] virtual std::string skippingTwin() const { return {}; }
    // The design choices that make this encoding, as (setting, value) pairs for people, e.g.
    // ("Layout", "inline") and ("Mipmap", "on"). No two encodings have the same list. The window
    // builds one switch per setting from these lists (EncodingChoices), so it never names an encoding.
    [[nodiscard]] virtual std::vector<EncodingSetting> settings() const { return {}; }
};

using EncodingRegistry = core::Registry<IEncoding>;

// The settings of every encoding of a registry, and the switches they make. A switch is one
// setting with every value that some encoding has. Picking a value picks the encoding that has
// that value and keeps as many of the other settings as it can.
class EncodingChoices {
public:
    struct Switch {
        std::string setting;
        std::vector<std::string> values; // in the order the encodings were registered
    };

    // One list of settings per encoding, in registry order.
    explicit EncodingChoices(std::vector<std::vector<EncodingSetting>> settingsOfEach);
    [[nodiscard]] static EncodingChoices of(const EncodingRegistry& registry);

    [[nodiscard]] const std::vector<Switch>& switches() const { return switches_; }
    [[nodiscard]] const std::vector<EncodingSetting>& settingsOf(std::size_t encoding) const
    {
        return settings_.at(encoding);
    }
    // The value of `setting` in `encoding`, or empty if that encoding has no such setting.
    [[nodiscard]] std::string valueOf(std::size_t encoding, std::string_view setting) const;
    // The encoding to show when the user sets `setting` to `value`, starting from `current`.
    // Throws std::invalid_argument if no encoding has that value.
    [[nodiscard]] std::size_t pick(std::size_t current, std::string_view setting, std::string_view value) const;
    // "Layout inline, Mipmap off": the settings of `encoding` for people.
    [[nodiscard]] std::string describe(std::size_t encoding) const;

private:
    std::vector<std::vector<EncodingSetting>> settings_;
    std::vector<Switch> switches_;
};

// The size of every buffer of an encoded scene, in the order the GPU gets them.
struct MemoryReport {
    struct Buffer {
        std::string name;
        std::uint64_t bytes = 0;
    };
    std::vector<Buffer> buffers; // the bounds first, then the buffers of the encoding
    std::uint64_t totalBytes = 0;

    [[nodiscard]] double bitsPerVoxel(std::uint64_t voxels) const;
    [[nodiscard]] nlohmann::json toJson(std::uint64_t voxels) const;
};

[[nodiscard]] MemoryReport memoryReport(const core::Scene& scene);

} // namespace thesis::encoding
