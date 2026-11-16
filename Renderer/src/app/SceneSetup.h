#pragma once

#include "Options.h"

#include "thesis/core/Camera.h"
#include "thesis/core/Scene.h"
#include "thesis/encoding/Encoding.h"
#include "thesis/tracing/Tracer.h"
#include "thesis/voxel/VoxelModel.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace thesis::app {

// A scene ready for the GPU, with the facts that the panel and the results files show.
struct BuiltScene {
    core::Scene scene;
    std::string encoding; // empty: the scene is not an encoded voxel model (the box field)
    std::string details;  // one line for people, e.g. the size of the voxel grid
    std::uint64_t voxels = 0;
    encoding::MemoryReport memory; // size of every buffer
    double encodeSeconds = 0.0;
    nlohmann::json statistics; // what the encoding reports about its data; null without encoding
};

// What the program shows: the box field of step 0, or a voxel model (--model) that can be
// encoded in several ways. The voxel model is read once and kept in memory; build() encodes it
// on request. That is what lets the window switch between encodings while it runs.
class SceneSource {
public:
    explicit SceneSource(const Options& options);

    [[nodiscard]] bool isVoxelModel() const { return model_.has_value(); }
    // The encodings to choose from, in registration order. Empty for the box field.
    [[nodiscard]] const std::vector<std::string>& encodings() const { return encodings_; }
    // Index into encodings() of the encoding named by --encoding (0 if none was named).
    [[nodiscard]] std::size_t initialEncoding() const { return initialEncoding_; }

    // `encoding` is an index into encodings(); it is ignored for the box field.
    [[nodiscard]] BuiltScene build(std::size_t encoding) const;

private:
    std::uint32_t gridSize_;
    std::uint32_t seed_;
    std::optional<voxel::VoxelModel> model_;
    encoding::EncodingRegistry registry_;
    std::vector<std::string> encodings_;
    std::size_t initialEncoding_ = 0;
};

// The camera at the start: the one that shows all of `bounds`, changed by --camera.
[[nodiscard]] core::OrbitCamera startCamera(const Options& options, const core::Bounds& bounds);

// All tracers built into the program, in registration order.
[[nodiscard]] tracing::TracerRegistry makeTracerRegistry();

// All encodings built into the program, in registration order.
[[nodiscard]] encoding::EncodingRegistry makeEncodingRegistry();

// Names from `requested`, or all registered names when it is empty. Throws on unknown names.
[[nodiscard]] std::vector<std::string> selectTracers(const tracing::TracerRegistry& registry,
                                                     const std::vector<std::string>& requested);

} // namespace thesis::app
