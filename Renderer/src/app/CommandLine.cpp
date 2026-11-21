#include "CommandLine.h"

#include "thesis/shared/RayOrder.h"

#include <CLI/CLI.hpp>

#include <cstdint>
#include <format>
#include <map>

namespace thesis::app {
namespace {

const std::map<std::string, std::uint32_t> kRayOrders{{"rows", THESIS_RAY_ORDER_ROWS},
                                                      {"tiles", THESIS_RAY_ORDER_TILES}};

// The shortest text that reads back as the same float (std::format guarantees the round trip).
std::string number(float value)
{
    return std::format("{}", value);
}

std::string pathText(const std::filesystem::path& path)
{
    return path.generic_string();
}

} // namespace

ParsedCommand parseCommandLine(const std::vector<std::string>& arguments)
{
    ParsedCommand parsed;
    Options& options = parsed.options;
    options.shaderDirectory = THESIS_DEFAULT_SHADER_DIR;

    // Kept alive while CLI11 reads it: --exact-camera has six numbers.
    std::vector<float> exactCamera;

    CLI::App cli{"Thesis renderer: hardware vs software ray tracing behind one tracer socket"};
    cli.add_flag("--benchmark", options.benchmark, "Run the headless benchmark and write results files");
    cli.add_flag("--fixed-view", options.fixedView, "Benchmark a stationary camera");
    cli.add_flag("--compare-skipping", options.compareSkipping, "Compare skipping off and on at one camera pose");
    cli.add_option("--open-result", options.openResult, "Reopen the scene and pose of a fixed-view benchmark")
        ->check(CLI::ExistingFile);
    cli.add_option("--screenshot", options.screenshot, "Write one frame to this PNG file, without a window");
    cli.add_option("--view", options.view, "The view, numbered as in the panel of the window: 1 to 12")
        ->check(CLI::Range(1, 12));
    cli.add_option("--scale-max", options.scaleMaximum,
                   "Count views: the count at the end of the colour map; a larger count is white")
        ->check(CLI::Range(1.0f, 4096.0f));
    cli.add_flag("--log-scale", options.logScale, "Count views: log2 colour scale instead of linear");
    cli.add_option("--light", options.light, "Lit views: azimuth and elevation of the light in degrees")->expected(2);
    cli.add_option("--distance-range", options.distanceRange,
                   "Distance view: the distances shown as white and as black")
        ->expected(2);
    cli.add_option("--inspect", options.inspect,
                   "The ray inspector on pixel x y of the frame. Window: starts on that ray. Screenshot: also "
                   "prints every step of that ray")
        ->expected(2);
    cli.add_option("--camera", options.camera,
                   "Camera at the start: yaw and pitch in degrees, then the distance as a factor of the "
                   "distance that shows the whole scene. Benchmark: the start and the radius of the orbit")
        ->expected(3);
    cli.add_option("--exact-camera", exactCamera,
                   "The camera as the window has it: target x y z, yaw and pitch in degrees, distance")
        ->expected(6)
        ->excludes("--camera");
    cli.add_flag("--list-tracers", options.listTracers, "List the tracers built into this program");
    cli.add_flag("--list-encodings", options.listEncodings, "List the encodings built into this program");
    cli.add_option("--tracer", options.tracers,
                   "Tracer(s) to use; benchmark default: all; screenshot default: the "
                   "default tracer");
    cli.add_option("--model", options.model, "Voxel file (.voxels, written by thesis_model) to show")
        ->check(CLI::ExistingFile);
    cli.add_option("--encoding", options.encoding, "Encoding of the voxel model; window: the one shown first");
    cli.add_option("--brick-size", options.brickSize,
                   "Voxels along one side of a brick: 8, 16 or 32. Default: as in the voxel file")
        ->check(CLI::IsMember({8, 16, 32}));
    cli.add_option("--grid", options.gridSize, "Box field: boxes per side")->check(CLI::PositiveNumber);
    cli.add_option("--seed", options.seed, "Box field: random seed");
    cli.add_flag("--boxes", options.boxExample, "Open the box tracing example")->excludes("--model");
    cli.add_option("--width", options.width, "Render width (rays per row)")->check(CLI::PositiveNumber);
    cli.add_option("--height", options.height, "Render height")->check(CLI::PositiveNumber);
    cli.add_option("--ray-order", options.rayOrder,
                   "Where the ray of a pixel sits in the ray list: rows (row by row) or tiles (8 x 4 pixels per "
                   "warp)")
        ->transform(CLI::CheckedTransformer(kRayOrders));
    cli.add_option("--warmup", options.warmupSeconds, "Benchmark: minimum warm-up seconds for each tracer")
        ->check(CLI::NonNegativeNumber);
    cli.add_option("--warmup-max", options.warmupMaxSeconds,
                   "Benchmark: maximum warm-up seconds while waiting for the GPU clock and the trace time to settle")
        ->check(CLI::NonNegativeNumber);
    cli.add_option("--frames", options.frames, "Benchmark: frames per repeat")->check(CLI::PositiveNumber);
    cli.add_option("--repeats", options.repeats, "Benchmark: repeats per tracer")->check(CLI::PositiveNumber);
    cli.add_option("--cooldown", options.cooldownSeconds, "Benchmark: seconds of idle time between tracers");
    cli.add_option("--output", options.outputDirectory, "Benchmark: directory for results files");
    cli.add_flag("--validation", options.validation, "Enable the Vulkan validation layer");
    cli.add_option("--exit-after-frames", options.exitAfterFrames, "Window: close after N frames (smoke test)");
    cli.add_flag("--self-test", options.selfTest,
                 "Window: use every control of the panel once, check \"Copy as command\" against the frame on "
                 "screen, then close (smoke test of the window)");
    cli.add_option("--shader-dir", options.shaderDirectory, "Directory with the compiled shaders");

    // CLI11 reads a vector of arguments from the back, so give it argc / argv instead.
    std::vector<const char*> argv{"thesis_renderer"};
    for (const std::string& argument : arguments) {
        argv.push_back(argument.c_str());
    }
    try {
        cli.parse(static_cast<int>(argv.size()), argv.data());
    } catch (const CLI::ParseError& error) {
        parsed.exitCode = cli.exit(error);
        return parsed;
    }

    options.boxExample = options.boxExample || (options.model.empty() && (cli.count("--grid") || cli.count("--seed")));
    if (exactCamera.size() == 6) {
        core::OrbitCamera camera;
        camera.target = {exactCamera[0], exactCamera[1], exactCamera[2]};
        camera.yawDegrees = exactCamera[3];
        camera.pitchDegrees = exactCamera[4];
        camera.distance = exactCamera[5];
        options.exactCamera = camera;
    }
    return parsed;
}

std::vector<std::string> commandArguments(const Options& options, CommandForm form)
{
    const Options defaults;
    std::vector<std::string> arguments;
    const auto add = [&](std::initializer_list<std::string> words) {
        arguments.insert(arguments.end(), words.begin(), words.end());
    };

    if (form == CommandForm::Benchmark) {
        add({"--benchmark"});
        if (options.fixedView) {
            add({"--fixed-view"});
        }
    } else {
        add({"--screenshot", options.screenshot.empty() ? std::string("frame.png") : pathText(options.screenshot)});
    }

    if (!options.model.empty()) {
        add({"--model", pathText(options.model)});
        if (!options.encoding.empty()) {
            add({"--encoding", options.encoding});
        }
        if (options.brickSize != 0) {
            add({"--brick-size", std::to_string(options.brickSize)});
        }
    } else {
        add({"--boxes"});
        if (options.gridSize != defaults.gridSize) {
            add({"--grid", std::to_string(options.gridSize)});
        }
        if (options.seed != defaults.seed) {
            add({"--seed", std::to_string(options.seed)});
        }
    }
    for (const std::string& tracer : options.tracers) {
        add({"--tracer", tracer});
    }
    for (const auto& [name, order] : kRayOrders) {
        if (order == options.rayOrder && order != defaults.rayOrder) {
            add({"--ray-order", name});
        }
    }
    if (options.width != defaults.width || options.height != defaults.height) {
        add({"--width", std::to_string(options.width), "--height", std::to_string(options.height)});
    }

    if (options.exactCamera) {
        const core::OrbitCamera& camera = *options.exactCamera;
        add({"--exact-camera", number(camera.target.x), number(camera.target.y), number(camera.target.z),
             number(camera.yawDegrees), number(camera.pitchDegrees), number(camera.distance)});
    } else {
        add({"--camera", number(options.camera.at(0)), number(options.camera.at(1)), number(options.camera.at(2))});
    }

    if (form == CommandForm::Screenshot) {
        if (options.view != defaults.view) {
            add({"--view", std::to_string(options.view)});
        }
        if (options.scaleMaximum != defaults.scaleMaximum) {
            add({"--scale-max", number(options.scaleMaximum)});
        }
        if (options.logScale) {
            add({"--log-scale"});
        }
        if (options.light.size() == 2) {
            add({"--light", number(options.light[0]), number(options.light[1])});
        }
        if (options.distanceRange.size() == 2) {
            add({"--distance-range", number(options.distanceRange[0]), number(options.distanceRange[1])});
        }
        if (options.inspect.size() == 2) {
            add({"--inspect", std::to_string(options.inspect[0]), std::to_string(options.inspect[1])});
        }
    }
    return arguments;
}

std::string commandLine(const std::string& program, const std::vector<std::string>& arguments)
{
    const auto quoted = [](const std::string& word) {
        return word.find_first_of(" \t\"") == std::string::npos ? word : "\"" + word + "\"";
    };
    std::string line = quoted(program);
    for (const std::string& argument : arguments) {
        line += " " + quoted(argument);
    }
    return line;
}

} // namespace thesis::app
