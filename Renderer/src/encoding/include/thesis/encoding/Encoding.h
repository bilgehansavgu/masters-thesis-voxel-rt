#pragma once

// ============================================================================
// READING ORDER: FILE 6 (The `IEncoding` Socket: VoxelModel <-> GPU Scene)
// ============================================================================
// Just as `ITracer` is the plug-in socket for ray tracers, `IEncoding` is the plug-in socket
// for compressed voxel formats (`brick2012_inline`, `brick2012_split`, and later a baseline
// format in Step 5).
//
// Every encoding must obey two contracts:
//   1. `encode(model)` produces a `core::Scene` where each brick is one primitive (`primitiveBounds`
//      at slot 8) and the encoding's packed byte buffers sit in `primitiveData` (slots 9..15),
//      with `primitiveKind` set to the encoding's name so the matching shader
//      `shaders/primitives/<name>/primitive.slang` is used.
//   2. Lossless round-trip: `decode(encode(model).scene)` must reconstruct the exact same filled
//      voxels and 16-bit normals (`sameVoxels(decoded, model) == true`).

#include "thesis/core/Registry.h"
#include "thesis/core/Scene.h"
#include "thesis/voxel/VoxelModel.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace thesis::encoding {

// What an encoding makes of a voxel model.
struct EncodedModel {
    // One primitive per brick. `primitiveBounds` are the boxes around the filled voxels of each
    // brick, `primitiveData` are the buffers of the encoding, and `primitiveKind` is the name of
    // the encoding. Ready for tracing::GpuScene::upload.
    core::Scene scene;
    // Numbers that describe the encoded data, e.g. how many cells are of each kind.
    nlohmann::json statistics;
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
};

using EncodingRegistry = core::Registry<IEncoding>;

// The size of every buffer of an encoded scene, in the order the GPU gets them.
struct MemoryReport {
    struct Buffer {
        std::string name;
        std::uint64_t bytes = 0;
    };
    std::vector<Buffer> buffers; // the bounds first, then the buffers of the encoding
    std::uint64_t totalBytes = 0;

    // Total GPU memory in bits divided by the number of filled surface voxels.
    [[nodiscard]] double bitsPerVoxel(std::uint64_t voxels) const;
    [[nodiscard]] nlohmann::json toJson(std::uint64_t voxels) const;
};

[[nodiscard]] MemoryReport memoryReport(const core::Scene& scene);

} // namespace thesis::encoding
