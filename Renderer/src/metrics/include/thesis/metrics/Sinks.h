#pragma once

#include "thesis/metrics/MetricSink.h"

#include <filesystem>
#include <vector>

namespace thesis::metrics {

// Live plots on the Tracy timeline (one plot per stage, plus ray throughput).
class TracySink final : public IMetricSink {
public:
    void beginRun(const nlohmann::json& run) override;
    void recordFrame(const FrameSample& frame) override;
    void endRun(const nlohmann::json& summary) override;
};

// Writes one JSON file per run: {run, frames[], summary}. Thesis tables are made from these.
class JsonReportSink final : public IMetricSink {
public:
    explicit JsonReportSink(std::filesystem::path path);
    void beginRun(const nlohmann::json& run) override;
    void recordFrame(const FrameSample& frame) override;
    void endRun(const nlohmann::json& summary) override;

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
    nlohmann::json report_;
};

// Forwards everything to several sinks.
class MetricFanout final : public IMetricSink {
public:
    explicit MetricFanout(std::vector<IMetricSink*> sinks);
    void beginRun(const nlohmann::json& run) override;
    void recordFrame(const FrameSample& frame) override;
    void endRun(const nlohmann::json& summary) override;

private:
    std::vector<IMetricSink*> sinks_;
};

} // namespace thesis::metrics
