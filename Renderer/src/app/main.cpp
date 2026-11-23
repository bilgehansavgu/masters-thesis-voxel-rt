#include "Benchmark.h"
#include "CommandLine.h"
#include "Interactive.h"
#include "Options.h"
#include "SceneSetup.h"
#include "Screenshot.h"
#include "TracerSetup.h"

#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    const std::vector<std::string> arguments(argv + 1, argv + argc);
    thesis::app::ParsedCommand parsed = thesis::app::parseCommandLine(arguments);
    if (parsed.exitCode) {
        return *parsed.exitCode;
    }
    thesis::app::Options& options = parsed.options;
    options.program = std::filesystem::absolute(argv[0]);

    try {
        if (!options.openResult.empty())
            thesis::app::restoreResult(options);
        if (options.compareSkipping) {
            auto comparison = thesis::app::compareView(options);
            return comparison.hits.agrees() ? 0 : 2;
        }
        if (options.listTracers) {
            const auto registry = thesis::app::makeTracerRegistry(); // must outlive the loop
            for (const auto& entry : registry.entries()) {
                std::printf("%-16s %s\n", entry.name.c_str(), entry.description.c_str());
            }
            return 0;
        }
        if (options.listEncodings) {
            const auto registry = thesis::app::makeEncodingRegistry();
            for (const auto& entry : registry.entries()) {
                const auto encoding = registry.create(entry.name);
                std::string settings;
                for (const auto& setting : encoding->settings()) {
                    settings += (settings.empty() ? "" : ", ") + setting.name + " " + setting.value;
                }
                std::printf("%-22s %-28s %s\n", entry.name.c_str(), settings.c_str(), entry.description.c_str());
            }
            return 0;
        }
        if (!options.screenshot.empty()) {
            return thesis::app::runScreenshot(options);
        }
        return options.benchmark ? thesis::app::runBenchmark(options) : thesis::app::runInteractive(options);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "error: %s\n", error.what());
        return 1;
    }
}
