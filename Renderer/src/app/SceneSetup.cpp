#include "SceneSetup.h"

#include "thesis/core/BoxField.h"
#include "thesis/encodings/BuiltInEncodings.h"
#include "thesis/tracers/BuiltInTracers.h"
#include "thesis/voxel/Rebrick.h"
#include "thesis/voxel/VoxelFile.h"

#include <glm/trigonometric.hpp>

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

    modelFile_ = options.model;
    model_ = std::make_shared<const voxel::VoxelModel>(voxel::loadVoxelModel(options.model));
    registry_ = makeEncodingRegistry();
    for (const auto& entry : registry_.entries()) {
        const auto encoding = registry_.create(entry.name);
        encodings_.push_back({entry.name, entry.description, encoding->skipsEmptySpace(), encoding->skippingTwin()});
    }
    if (encodings_.empty()) {
        throw std::runtime_error("No encoding is built into this program");
    }
    choices_ = encoding::EncodingChoices::of(registry_);
    initialEncoding_ = encodingIndex(options.encoding.empty() ? registry_.defaultName() : options.encoding);

    brickSizes_ = {8, 16, 32};
    initialBrickSize_ = options.brickSize != 0 ? options.brickSize : model_->grid.brickSize;
    if (!voxel::isSupportedBrickSize(initialBrickSize_)) {
        throw std::invalid_argument("--brick-size must be 8, 16 or 32");
    }
}

std::size_t SceneSource::encodingIndex(const std::string& name) const
{
    const auto it = std::find_if(encodings_.begin(), encodings_.end(),
                                 [&](const EncodingChoice& choice) { return choice.name == name; });
    if (it == encodings_.end()) {
        throw std::invalid_argument("Unknown encoding '" + name + "'. Use --list-encodings to see the options.");
    }
    return static_cast<std::size_t>(it - encodings_.begin());
}

std::shared_ptr<const voxel::VoxelModel> SceneSource::modelWithBricksOf(std::uint32_t brickSize)
{
    if (brickSize == model_->grid.brickSize) {
        return model_;
    }
    if (!regrouped_ || regrouped_->grid.brickSize != brickSize) {
        regrouped_.reset(); // free the old copy first: a large model does not fit three times
        regrouped_ = std::make_shared<const voxel::VoxelModel>(voxel::rebrick(*model_, brickSize));
    }
    return regrouped_;
}

const voxel::VoxelBrick* findBrick(const voxel::VoxelModel& model, glm::uvec3 coordinate)
{
    // The bricks are stored in ascending order of x + sizeX * (y + sizeY * z) (VoxelModel.h).
    const glm::uvec3 bricks = model.grid.size / model.grid.brickSize;
    const auto key = [&](glm::uvec3 c) {
        return static_cast<std::uint64_t>(c.x) + bricks.x * (static_cast<std::uint64_t>(c.y) + bricks.y * c.z);
    };
    const std::uint64_t wanted = key(coordinate);
    const auto found =
        std::lower_bound(model.bricks.begin(), model.bricks.end(), wanted,
                         [&](const voxel::VoxelBrick& brick, std::uint64_t k) { return key(brick.coordinate) < k; });
    return found != model.bricks.end() && found->coordinate == coordinate ? &*found : nullptr;
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
    built.model = modelWithBricksOf(brickSize);
    const voxel::VoxelModel& model = *built.model;
    const auto start = std::chrono::steady_clock::now();
    encoding::EncodedModel encoded = encoder->encode(model);
    built.encodeSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    built.scene = std::move(encoded.scene);
    built.statistics = std::move(encoded.statistics);
    built.bricks = std::move(encoded.bricks);
    built.encoding = name;
    built.brickSize = model.grid.brickSize;
    built.voxels = model.voxelCount();
    built.details = std::format("{} x {} x {} voxels, bricks of {}, rule {}", model.grid.size.x, model.grid.size.y,
                                model.grid.size.z, model.grid.brickSize, model.source.rule);
    built.memory = encoding::memoryReport(built.scene);
    return built;
}

std::vector<std::filesystem::path> listModels(const std::filesystem::path& directory)
{
    std::vector<std::filesystem::path> models;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        if (entry.is_regular_file() && entry.path().extension() == ".voxels") {
            models.push_back(entry.path().lexically_normal());
        }
    }
    std::sort(models.begin(), models.end());
    return models;
}

core::OrbitCamera startCamera(const Options& options, const core::Bounds& bounds)
{
    if (options.exactCamera)
        return *options.exactCamera;
    core::OrbitCamera camera = core::OrbitCamera::framing(bounds);
    camera.yawDegrees = options.camera.at(0);
    camera.pitchDegrees = options.camera.at(1);
    camera.distance *= options.camera.at(2);
    return camera;
}

resolve::ShadingParams startShading(const Options& options, const core::OrbitCamera& camera)
{
    resolve::ShadingParams shading;
    shading.mode = static_cast<resolve::ShadingMode>(options.view - 1);
    shading.distanceFar = 2.0f * camera.distance;
    if (options.distanceRange.size() == 2) {
        shading.distanceNear = options.distanceRange[0];
        shading.distanceFar = options.distanceRange[1];
    }
    shading.workScale.maximum = options.scaleMaximum;
    shading.workScale.kind = options.logScale ? resolve::ScaleKind::Log2 : resolve::ScaleKind::Linear;
    if (options.light.size() == 2) {
        shading.light.azimuth = glm::radians(options.light[0]);
        shading.light.elevation = glm::radians(options.light[1]);
    }
    return shading;
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
