#include "thesis/tracing/GpuScene.h"

#include "thesis/tracing/Bindings.h"

#include <set>
#include <span>
#include <stdexcept>

namespace thesis::tracing {

GpuScene GpuScene::upload(const gpu::Context& context, const core::Scene& scene)
{
    if (scene.primitiveBounds.empty()) {
        throw std::invalid_argument("GpuScene: scene '" + scene.name + "' has no primitives");
    }
    GpuScene result;
    result.name = scene.name;
    result.primitiveKind = scene.primitiveKind;
    result.primitiveCount = static_cast<std::uint32_t>(scene.primitiveBounds.size());
    result.bounds = scene.bounds;
    result.primitiveGrid = scene.primitiveGrid;
    result.primitiveBounds =
        gpu::uploadBuffer(context, std::as_bytes(std::span(scene.primitiveBounds)),
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                              VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);

    std::set<std::uint32_t> used;
    for (const core::PrimitiveData& data : scene.primitiveData) {
        if (data.binding < kPrimitiveDataBindingFirst || data.binding > kPrimitiveDataBindingLast) {
            throw std::invalid_argument("GpuScene: primitive data '" + data.name + "' uses binding " +
                                        std::to_string(data.binding) + ", outside the range reserved for it");
        }
        if (!used.insert(data.binding).second) {
            throw std::invalid_argument("GpuScene: two primitive data buffers use binding " +
                                        std::to_string(data.binding));
        }
        if (data.bytes.empty()) {
            throw std::invalid_argument("GpuScene: primitive data '" + data.name + "' is empty");
        }
        result.primitiveData.push_back(
            {data.binding, data.name,
             gpu::uploadBuffer(context, data.bytes,
                               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)});
    }
    return result;
}

std::vector<gpu::KernelBinding> GpuScene::primitiveBindings() const
{
    std::vector<gpu::KernelBinding> bindings{{kPrimitiveBoundsBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}};
    for (const DataBuffer& data : primitiveData) {
        bindings.push_back({data.binding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER});
    }
    return bindings;
}

void GpuScene::bindPrimitiveData(gpu::KernelResources& resources) const
{
    resources.buffer(kPrimitiveBoundsBinding, primitiveBounds);
    for (const DataBuffer& data : primitiveData) {
        resources.buffer(data.binding, data.buffer);
    }
}

std::uint64_t GpuScene::memoryBytes() const
{
    std::uint64_t bytes = primitiveBounds.size();
    for (const DataBuffer& data : primitiveData) {
        bytes += data.buffer.size();
    }
    return bytes;
}

} // namespace thesis::tracing
