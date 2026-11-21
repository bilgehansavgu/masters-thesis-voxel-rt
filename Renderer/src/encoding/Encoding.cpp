#include "thesis/encoding/Encoding.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace thesis::encoding {

EncodingChoices::EncodingChoices(std::vector<std::vector<EncodingSetting>> settingsOfEach)
    : settings_(std::move(settingsOfEach))
{
    for (std::size_t i = 0; i < settings_.size(); ++i) {
        for (std::size_t j = 0; j < i; ++j) {
            // Two encodings with the same settings could not be told apart by the switches.
            if (settings_[i] == settings_[j]) {
                throw std::logic_error("EncodingChoices: encodings " + std::to_string(j) + " and " + std::to_string(i) +
                                       " have the same settings");
            }
        }
        for (const EncodingSetting& setting : settings_[i]) {
            auto it = std::find_if(switches_.begin(), switches_.end(),
                                   [&](const Switch& existing) { return existing.setting == setting.name; });
            if (it == switches_.end()) {
                switches_.push_back({setting.name, {}});
                it = std::prev(switches_.end());
            }
            if (std::find(it->values.begin(), it->values.end(), setting.value) == it->values.end()) {
                it->values.push_back(setting.value);
            }
        }
    }
}

EncodingChoices EncodingChoices::of(const EncodingRegistry& registry)
{
    std::vector<std::vector<EncodingSetting>> settings;
    for (const auto& entry : registry.entries()) {
        settings.push_back(registry.create(entry.name)->settings());
    }
    return EncodingChoices(std::move(settings));
}

std::string EncodingChoices::valueOf(std::size_t encoding, std::string_view setting) const
{
    for (const EncodingSetting& own : settings_.at(encoding)) {
        if (own.name == setting) {
            return own.value;
        }
    }
    return {};
}

std::size_t EncodingChoices::pick(std::size_t current, std::string_view setting, std::string_view value) const
{
    std::size_t best = settings_.size();
    std::size_t bestShared = 0;
    for (std::size_t i = 0; i < settings_.size(); ++i) {
        if (valueOf(i, setting) != value) {
            continue;
        }
        // How many settings this encoding shares with the current one.
        const auto shared = static_cast<std::size_t>(
            std::count_if(settings_[i].begin(), settings_[i].end(),
                          [&](const EncodingSetting& own) { return valueOf(current, own.name) == own.value; }));
        if (best == settings_.size() || shared > bestShared) {
            best = i;
            bestShared = shared;
        }
    }
    if (best == settings_.size()) {
        throw std::invalid_argument("No encoding has " + std::string(setting) + " " + std::string(value));
    }
    return best;
}

std::string EncodingChoices::describe(std::size_t encoding) const
{
    std::string text;
    for (const EncodingSetting& setting : settings_.at(encoding)) {
        text += (text.empty() ? "" : ", ") + setting.name + " " + setting.value;
    }
    return text;
}

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
