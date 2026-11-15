// ============================================================================
// READING ORDER: FILE 17 (Program entry point: parses CLI flags and picks a mode)
// ============================================================================

#include "Benchmark.h"
#include "Interactive.h"
#include "Options.h"
#include "SceneSetup.h"

#include <CLI/CLI.hpp> // CLI11 library for parsing command-line arguments (--benchmark, --width, etc.)

#include <cstdio>
#include <exception>

int main(int argc, char** argv)
{
    thesis::app::Options options;
    options.shaderDirectory = THESIS_DEFAULT_SHADER_DIR;

    CLI::App cli{"Thesis renderer: hardware vs software ray tracing behind one tracer socket"};
    cli.add_flag("--benchmark", options.benchmark, "Run the headless benchmark and write results files");
    cli.add_flag("--list-tracers", options.listTracers, "List the tracers built into this program");
    cli.add_option("--tracer", options.tracers, "Tracer(s) to use; benchmark default: all");
    cli.add_option("--grid", options.gridSize, "Box field: boxes per side")->check(CLI::PositiveNumber);
    cli.add_option("--seed", options.seed, "Box field: random seed");
    cli.add_option("--width", options.width, "Render width (rays per row)")->check(CLI::PositiveNumber);
    cli.add_option("--height", options.height, "Render height")->check(CLI::PositiveNumber);
    cli.add_option("--warmup", options.warmupSeconds, "Benchmark: minimum warm-up seconds per tracer")
        ->check(CLI::NonNegativeNumber);
    cli.add_option("--warmup-max", options.warmupMaxSeconds,
                   "Benchmark: maximum warm-up seconds while waiting for the GPU clock to settle")
        ->check(CLI::NonNegativeNumber);
    cli.add_option("--frames", options.frames, "Benchmark: frames per repeat (one orbit)")->check(CLI::PositiveNumber);
    cli.add_option("--repeats", options.repeats, "Benchmark: repeats per tracer")->check(CLI::PositiveNumber);
    cli.add_option("--cooldown", options.cooldownSeconds, "Benchmark: seconds of idle time between tracers");
    cli.add_option("--output", options.outputDirectory, "Benchmark: directory for results files");
    cli.add_flag("--validation", options.validation, "Enable the Vulkan validation layer");
    cli.add_option("--exit-after-frames", options.exitAfterFrames, "Window: close after N frames (smoke test)");
    cli.add_option("--shader-dir", options.shaderDirectory, "Directory with the compiled shaders");
    CLI11_PARSE(cli, argc, argv);

    try {
        if (options.listTracers) {
            const auto registry = thesis::app::makeTracerRegistry(); // must outlive the loop
            for (const auto& entry : registry.entries()) {
                std::printf("%-10s %s\n", entry.name.c_str(), entry.description.c_str());
            }
            return 0;
        }
        // Run either headless benchmark mode (`Benchmark.cpp`) or interactive window mode (`Interactive.cpp`).
        return options.benchmark ? thesis::app::runBenchmark(options) : thesis::app::runInteractive(options);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "error: %s\n", error.what());
        return 1;
    }
}
