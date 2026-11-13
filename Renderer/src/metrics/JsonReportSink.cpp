#include "thesis/metrics/Sinks.h"

#include <fstream>
#include <stdexcept>
#include <utility>

namespace thesis::metrics {

JsonReportSink::JsonReportSink(std::filesystem::path path) : path_(std::move(path))
{}

void JsonReportSink::beginRun(const nlohmann::json& run)
{
    report_ = nlohmann::json::object();
    report_["run"] = run;
    report_["frames"] = nlohmann::json::array();
}

void JsonReportSink::recordFrame(const FrameSample& frame)
{
    nlohmann::json stages = nlohmann::json::object();
    for (const StageSample& stage : frame.stages) {
        stages[stage.stage] = stage.milliseconds;
    }
    report_["frames"].push_back(
        {{"repeat", frame.repeat}, {"frame", frame.frame}, {"rays", frame.rayCount}, {"stages_ms", std::move(stages)}});
}

void JsonReportSink::endRun(const nlohmann::json& summary)
{
    report_["summary"] = summary;
    if (path_.has_parent_path()) {
        std::filesystem::create_directories(path_.parent_path());
    }
    std::ofstream file(path_);
    if (!file) {
        throw std::runtime_error("Cannot write results file '" + path_.string() + "'");
    }
    file << report_.dump(2) << '\n';
}

} // namespace thesis::metrics
