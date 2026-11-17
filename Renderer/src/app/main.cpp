#include "Benchmark.h"
#include "Interactive.h"
#include "Options.h"
#include "SceneSetup.h"
#include "Screenshot.h"

#include <CLI/CLI.hpp>

#include <cstdio>
#include <exception>

int main(int argc, char** argv)
{
    thesis::app::Options options;
    options.shaderDirectory = THESIS_DEFAULT_SHADER_DIR;

    CLI::App cli{"Thesis renderer: hardware vs software ray tracing behind one tracer socket"};
    cli.add_flag("--benchmark", options.benchmark, "Run the headless benchmark and write results files");
    cli.add_option("--screenshot", options.screenshot, "Write one frame to this PNG file, without a window");
    cli.add_option("--view", options.view, "Screenshot: the view, numbered as in the panel of the window: 1 to 12")
        ->check(CLI::Range(1, 12));
    cli.add_option("--inspect", options.inspect,
                   "The ray inspector on pixel x y of the frame. Window: starts on that ray. Screenshot: also "
                   "prints every step of that ray")
        ->expected(2);
    cli.add_option("--camera", options.camera,
                   "Camera at the start: yaw and pitch in degrees, then the distance as a factor of the "
                   "distance that shows the whole scene. Benchmark: the start and the radius of the orbit")
        ->expected(3);
    cli.add_flag("--list-tracers", options.listTracers, "List the tracers built into this program");
    cli.add_flag("--list-encodings", options.listEncodings, "List the encodings built into this program");
    cli.add_option("--tracer", options.tracers,
                   "Tracer(s) to use; benchmark default: all; screenshot default: hardware");
    cli.add_option("--model", options.model, "Voxel file (.voxels, written by thesis_model) to show")
        ->check(CLI::ExistingFile);
    cli.add_option("--encoding", options.encoding, "Encoding of the voxel model; window: the one shown first");
    cli.add_option("--brick-size", options.brickSize,
                   "Voxels along one side of a brick: 8, 16 or 32. Default: as in the voxel file")
        ->check(CLI::IsMember({8, 16, 32}));
    cli.add_option("--grid", options.gridSize, "Box field: boxes per side")->check(CLI::PositiveNumber);
    cli.add_option("--seed", options.seed, "Box field: random seed");
    cli.add_option("--width", options.width, "Render width (rays per row)")->check(CLI::PositiveNumber);
    cli.add_option("--height", options.height, "Render height")->check(CLI::PositiveNumber);
    cli.add_option("--warmup", options.warmupSeconds, "Benchmark: minimum warm-up seconds for each tracer")
        ->check(CLI::NonNegativeNumber);
    cli.add_option("--warmup-max", options.warmupMaxSeconds,
                   "Benchmark: maximum warm-up seconds while waiting for the GPU clock and the trace time to settle")
        ->check(CLI::NonNegativeNumber);
    cli.add_option("--frames", options.frames, "Benchmark: frames per repeat (one orbit)")->check(CLI::PositiveNumber);
    cli.add_option("--repeats", options.repeats, "Benchmark: repeats per tracer")->check(CLI::PositiveNumber);
    cli.add_option("--cooldown", options.cooldownSeconds, "Benchmark: seconds of idle time between tracers");
    cli.add_option("--output", options.outputDirectory, "Benchmark: directory for results files");
    cli.add_flag("--validation", options.validation, "Enable the Vulkan validation layer");
    cli.add_option("--exit-after-frames", options.exitAfterFrames, "Window: close after N frames (smoke test)");
    cli.add_flag("--self-test", options.selfTest,
                 "Window: use every control of the panel once, then close (smoke test of the window)");
    cli.add_option("--shader-dir", options.shaderDirectory, "Directory with the compiled shaders");
    CLI11_PARSE(cli, argc, argv);

    try {
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
                std::printf("%-22s %s\n", entry.name.c_str(), entry.description.c_str());
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
