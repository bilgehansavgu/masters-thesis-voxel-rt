#include "thesis/encoding/Encoding.h"

namespace thesis::encoding {

double MemoryReport::bitsPerVoxel(std::uint64_t voxels) const
{
    return voxels == 0 ? 0.0 : 8.0 * static_cast<double>(totalBytes) / static_cast<double>(voxels);
}

nlohmann::json MemoryReport::toJson(std::uint64_t voxels) const
{
    nlohmann::json list = nlohmann::json::array();
    for (const Buffer& buffer : buffers) {
        list.push_back({{"name", buffer.name}, {"bytes", buffer.bytes}});
    }
    return {{"buffers", std::move(list)}, {"total_bytes", totalBytes}, {"bits_per_voxel", bitsPerVoxel(voxels)}};
}

MemoryReport memoryReport(const core::Scene& scene)
{
    MemoryReport report;
    report.buffers.push_back({"bounds", scene.primitiveBounds.size() * sizeof(core::Aabb)});
    for (const core::PrimitiveData& data : scene.primitiveData) {
        report.buffers.push_back({data.name, data.bytes.size()});
    }
    for (const MemoryReport::Buffer& buffer : report.buffers) {
        report.totalBytes += buffer.bytes;
    }
    return report;
}

} // namespace thesis::encoding
