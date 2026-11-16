// ============================================================================
// READING ORDER: FILE 22B (after SceneSetup.h)
// ============================================================================
// PURPOSE:
// Implements `SceneSource` (loading `.voxels` and encoding on demand into a
// `core::Scene`) and the factory functions for `TracerRegistry` and
// `EncodingRegistry`.
// ============================================================================

#include "SceneSetup.h"

#include "thesis/core/BoxField.h"
#include "thesis/encodings/BuiltInEncodings.h"
#include "thesis/tracers/BuiltInTracers.h"
#include "thesis/voxel/VoxelFile.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <stdexcept>
#include <utility>

namespace thesis::app {

SceneSource::SceneSource(const Options& options) : gridSize_(options.gridSize), seed_(options.seed)
{
    if (options.model.empty()) {
        if (!options.encoding.empty()) {
            throw std::invalid_argument("--encoding needs a voxel model (--model)");
        }
        return;
    }

    model_ = voxel::loadVoxelModel(options.model);
    registry_ = makeEncodingRegistry();
    encodings_ = registry_.names();
    if (encodings_.empty()) {
        throw std::runtime_error("No encoding is built into this program");
    }
    if (!options.encoding.empty()) {
        const auto it = std::find(encodings_.begin(), encodings_.end(), options.encoding);
        if (it == encodings_.end()) {
            throw std::invalid_argument("Unknown encoding '" + options.encoding +
                                        "'. Use --list-encodings to see the options.");
        }
        initialEncoding_ = static_cast<std::size_t>(it - encodings_.begin());
    }
}

BuiltScene SceneSource::build(std::size_t encoding) const
{
    BuiltScene built;
    if (!model_) {
        core::BoxFieldOptions boxField;
        boxField.gridSize = gridSize_;
        boxField.seed = seed_;
        built.scene = core::makeBoxField(boxField);
        built.details = "plain boxes, no voxels";
        built.memory = encoding::memoryReport(built.scene);
        return built;
    }

    const std::string& name = encodings_.at(encoding);
    const auto encoder = registry_.create(name);
    const auto start = std::chrono::steady_clock::now();
    encoding::EncodedModel encoded = encoder->encode(*model_);
    built.encodeSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    built.scene = std::move(encoded.scene);
    built.statistics = std::move(encoded.statistics);
    built.encoding = name;
    built.voxels = model_->voxelCount();
    built.details = std::format("{} x {} x {} voxels, bricks of {}, rule {}", model_->grid.size.x, model_->grid.size.y,
                                model_->grid.size.z, model_->grid.brickSize, model_->source.rule);
    built.memory = encoding::memoryReport(built.scene);
    return built;
}

tracing::TracerRegistry makeTracerRegistry()
{
    tracing::TracerRegistry registry;
    plugins::registerBuiltInTracers(registry);
    return registry;
}

encoding::EncodingRegistry makeEncodingRegistry()
{
    encoding::EncodingRegistry registry;
    plugins::registerBuiltInEncodings(registry);
    return registry;
}

std::vector<std::string> selectTracers(const tracing::TracerRegistry& registry,
                                       const std::vector<std::string>& requested)
{
    if (requested.empty()) {
        return registry.names();
    }
    for (const std::string& name : requested) {
        if (!registry.contains(name)) {
            throw std::invalid_argument("Unknown tracer '" + name + "'. Use --list-tracers to see the options.");
        }
    }
    return requested;
}

} // namespace thesis::app
