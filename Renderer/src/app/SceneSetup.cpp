#include "SceneSetup.h"

#include "thesis/core/BoxField.h"
#include "thesis/encodings/BuiltInEncodings.h"
#include "thesis/tracers/BuiltInTracers.h"
#include "thesis/voxel/Rebrick.h"
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
        if (!options.encoding.empty() || options.brickSize != 0) {
            throw std::invalid_argument("--encoding and --brick-size need a voxel model (--model)");
        }
        return;
    }

    model_ = voxel::loadVoxelModel(options.model);
    registry_ = makeEncodingRegistry();
    for (const auto& entry : registry_.entries()) {
        encodings_.push_back({entry.name, entry.description});
    }
    if (encodings_.empty()) {
        throw std::runtime_error("No encoding is built into this program");
    }
    if (!options.encoding.empty()) {
        const auto it = std::find_if(encodings_.begin(), encodings_.end(),
                                     [&](const EncodingChoice& choice) { return choice.name == options.encoding; });
        if (it == encodings_.end()) {
            throw std::invalid_argument("Unknown encoding '" + options.encoding +
                                        "'. Use --list-encodings to see the options.");
        }
        initialEncoding_ = static_cast<std::size_t>(it - encodings_.begin());
    }

    brickSizes_ = {8, 16, 32};
    initialBrickSize_ = options.brickSize != 0 ? options.brickSize : model_->grid.brickSize;
    if (!voxel::isSupportedBrickSize(initialBrickSize_)) {
        throw std::invalid_argument("--brick-size must be 8, 16 or 32");
    }
}

const voxel::VoxelModel& SceneSource::modelWithBricksOf(std::uint32_t brickSize)
{
    if (brickSize == model_->grid.brickSize) {
        return *model_;
    }
    if (!regrouped_ || regrouped_->grid.brickSize != brickSize) {
        regrouped_.reset(); // free the old copy first: a large model does not fit three times
        regrouped_ = voxel::rebrick(*model_, brickSize);
    }
    return *regrouped_;
}

BuiltScene SceneSource::build(std::size_t encoding, std::uint32_t brickSize)
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

    const std::string& name = encodings_.at(encoding).name;
    const auto encoder = registry_.create(name);
    const voxel::VoxelModel& model = modelWithBricksOf(brickSize);
    const auto start = std::chrono::steady_clock::now();
    encoding::EncodedModel encoded = encoder->encode(model);
    built.encodeSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    built.scene = std::move(encoded.scene);
    built.statistics = std::move(encoded.statistics);
    built.encoding = name;
    built.brickSize = model.grid.brickSize;
    built.voxels = model.voxelCount();
    built.details = std::format("{} x {} x {} voxels, bricks of {}, rule {}", model.grid.size.x, model.grid.size.y,
                                model.grid.size.z, model.grid.brickSize, model.source.rule);
    built.memory = encoding::memoryReport(built.scene);
    return built;
}

core::OrbitCamera startCamera(const Options& options, const core::Bounds& bounds)
{
    core::OrbitCamera camera = core::OrbitCamera::framing(bounds);
    camera.yawDegrees = options.camera.at(0);
    camera.pitchDegrees = options.camera.at(1);
    camera.distance *= options.camera.at(2);
    return camera;
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
