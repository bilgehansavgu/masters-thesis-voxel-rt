#include "thesis/gpu/GpuTimer.h"

#include "thesis/gpu/Check.h"
#include "thesis/gpu/Context.h"

#include <stdexcept>

namespace thesis::gpu {

GpuTimer::GpuTimer(const Context& context, std::uint32_t maxScopes)
    : device_(context.device()), capacity_(2 * maxScopes), periodNs_(context.info().timestampPeriodNs)
{
    const std::uint32_t bits = context.info().timestampValidBits;
    validMask_ = bits >= 64 ? ~0ull : ((1ull << bits) - 1);

    VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    info.queryCount = capacity_;
    THESIS_VK_CHECK(vkCreateQueryPool(device_, &info, nullptr, &pool_));
}

GpuTimer::~GpuTimer()
{
    vkDestroyQueryPool(device_, pool_, nullptr);
}

void GpuTimer::reset(VkCommandBuffer cmd)
{
    vkCmdResetQueryPool(cmd, pool_, 0, capacity_);
    stages_.clear();
}

std::uint32_t GpuTimer::begin(VkCommandBuffer cmd, const char* stage)
{
    const auto scope = static_cast<std::uint32_t>(stages_.size());
    if (2 * scope + 1 >= capacity_) {
        throw std::logic_error("GpuTimer: too many scopes in one frame");
    }
    stages_.push_back(stage);
    // ALL_COMMANDS: the timestamp is written once everything recorded before it has finished,
    // so a scope measures exactly the work recorded inside it.
    vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, pool_, 2 * scope);
    return scope;
}

void GpuTimer::end(VkCommandBuffer cmd, std::uint32_t scope)
{
    vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, pool_, 2 * scope + 1);
}

std::vector<StageTiming> GpuTimer::resolve()
{
    std::vector<StageTiming> result;
    if (stages_.empty()) {
        return result;
    }
    const auto queryCount = static_cast<std::uint32_t>(2 * stages_.size());
    std::vector<std::uint64_t> ticks(queryCount);
    THESIS_VK_CHECK(vkGetQueryPoolResults(device_, pool_, 0, queryCount, ticks.size() * sizeof(std::uint64_t),
                                          ticks.data(), sizeof(std::uint64_t),
                                          VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
    result.reserve(stages_.size());
    for (std::size_t i = 0; i < stages_.size(); ++i) {
        const std::uint64_t begin = ticks[2 * i] & validMask_;
        const std::uint64_t end = ticks[2 * i + 1] & validMask_;
        const std::uint64_t delta = (end - begin) & validMask_; // tolerate counter wrap-around
        result.push_back({stages_[i], static_cast<double>(delta) * periodNs_ * 1e-6});
    }
    return result;
}

} // namespace thesis::gpu
