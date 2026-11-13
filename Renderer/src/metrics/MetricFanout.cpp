#include "thesis/metrics/Sinks.h"

#include <utility>

namespace thesis::metrics {

MetricFanout::MetricFanout(std::vector<IMetricSink*> sinks) : sinks_(std::move(sinks))
{}

void MetricFanout::beginRun(const nlohmann::json& run)
{
    for (IMetricSink* sink : sinks_) {
        sink->beginRun(run);
    }
}

void MetricFanout::recordFrame(const FrameSample& frame)
{
    for (IMetricSink* sink : sinks_) {
        sink->recordFrame(frame);
    }
}

void MetricFanout::endRun(const nlohmann::json& summary)
{
    for (IMetricSink* sink : sinks_) {
        sink->endRun(summary);
    }
}

} // namespace thesis::metrics
