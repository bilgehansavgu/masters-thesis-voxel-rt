#include "thesis/metrics/Sinks.h"

#include "thesis/profiling/Profiling.h"

#include <string>

namespace thesis::metrics {

void TracySink::beginRun(const nlohmann::json& run)
{
    const std::string text = "run: " + run.dump();
    THESIS_MESSAGE(text.data(), text.size());
}

void TracySink::recordFrame(const FrameSample& frame)
{
    for (const StageSample& stage : frame.stages) {
        THESIS_PLOT(stage.stage, stage.milliseconds);
    }
}

void TracySink::endRun(const nlohmann::json& summary)
{
    const std::string text = "summary: " + summary.dump();
    THESIS_MESSAGE(text.data(), text.size());
}

} // namespace thesis::metrics
