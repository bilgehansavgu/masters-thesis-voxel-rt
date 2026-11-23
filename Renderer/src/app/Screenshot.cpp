#include "Screenshot.h"

#include "FramePipeline.h"
#include "Gui.h"
#include "PathText.h"
#include "RayLogText.h"
#include "SceneSetup.h"
#include "TracerSetup.h"

#include "thesis/gpu/Context.h"
#include "thesis/gpu/Image.h"
#include "thesis/resolve/ViewSupport.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace thesis::app {

RenderedFrame renderFrame(const gpu::Context& context, const Options& options)
{
    SceneSource source(options);
    const BuiltScene built = source.build();
    const tracing::GpuScene gpuScene = tracing::GpuScene::upload(context, built.scene);
    const tracing::TracerRegistry registry = makeTracerRegistry();
    RenderedFrame frame;
    frame.tracer = options.tracers.empty() ? registry.defaultName() : selectTracers(registry, options.tracers).front();
    frame.sceneName = built.scene.name;
    frame.encoding = built.encoding;
    const auto tracer = registry.create(frame.tracer, tracing::TracerEnvironment{context, options.shaderDirectory});
    tracer->prepare(gpuScene);

    FramePipeline pipeline(context, options.shaderDirectory, {options.width, options.height},
                           static_cast<tracing::RayOrder>(options.rayOrder));
    const core::OrbitCamera camera = startCamera(options, built.scene.bounds);
    frame.shading = startShading(options, camera, context.info().subgroupSize);
    // No silent fallback: a view this tracer or scene cannot show is an error, as in the window.
    const resolve::ViewSupport support{
        tracer->countsWork(), tracer->countsStructureSteps(), tracer->countsCycles(), built.voxels > 0,
        !built.encoding.empty() && source.encodings().at(source.encodingIndex(built.encoding)).skipsEmptySpace};
    if (const char* reason = resolve::unavailableView(frame.shading.mode, support)) {
        const char* view = resolve::kShadingModeNames.at(static_cast<std::size_t>(frame.shading.mode));
        throw std::invalid_argument(std::string("--view '") + view + "' with tracer '" + frame.tracer + "': " + reason);
    }
    if (frame.shading.warpSize > 0 && !tracer->warpsFollowRayList()) {
        throw std::invalid_argument("--warp-max: tracer '" + frame.tracer +
                                    "' does not place the rays of the list on warps in order");
    }

    const bool inspect = options.inspect.size() == 2;
    if (inspect) {
        if (options.inspect[0] >= options.width || options.inspect[1] >= options.height) {
            throw std::invalid_argument("--inspect: the pixel is outside the frame");
        }
        if (!tracer->countsWork()) {
            throw std::invalid_argument("--inspect: tracer '" + frame.tracer + "' does not count work");
        }
        pipeline.setLoggedRay(pipeline.batch().rayOf(options.inspect[0], options.inspect[1]));
    }

    context.submitAndWait([&](VkCommandBuffer cmd) {
        pipeline.begin(cmd);
        pipeline.trace(cmd, *tracer, camera.frame(pipeline.aspectRatio()),
                       resolve::showsWork(frame.shading.mode) || inspect);
        pipeline.shade(cmd, frame.shading);
    });
    (void)pipeline.collectTimings();

    frame.log = pipeline.rayLog();
    if (frame.log) {
        frame.logText = rayLogAsText(describeRayLog(
            *frame.log, [&](const core::RayLogEntry& step) { return tracer->describeStructureStep(step); }));
    }
    frame.image = {options.width, options.height, gpu::downloadImage(context, pipeline.output())};
    return frame;
}

void writeFigure(const resolve::RgbaImage& frame, const resolve::ShadingParams& shading,
                 const std::filesystem::path& file)
{
    resolve::RgbaImage image = frame;
    const resolve::ViewLegend legend = resolve::legendFor(shading);
    if (!legend.empty()) {
        const std::filesystem::path font =
            std::filesystem::is_regular_file(kInterfaceFontFile) ? kInterfaceFontFile : std::filesystem::path{};
        image = resolve::withLegend(image, legend, font);
    }
    const std::string text = utf8(file);
    // WHY stbi_write_png_to_func: stbi_write_png opens the file with a narrow path, which fails
    // for characters outside the code page of Windows.
    std::ofstream out(file, std::ios::binary);
    const int written =
        out && stbi_write_png_to_func(
                   [](void* context, void* data, int size) {
                       static_cast<std::ofstream*>(context)->write(static_cast<const char*>(data), size);
                   },
                   &out, static_cast<int>(image.width), static_cast<int>(image.height), 4, image.pixels.data(),
                   static_cast<int>(image.width) * 4);
    if (written == 0 || !out) {
        throw std::runtime_error("Cannot write '" + text + "'");
    }
}

int runScreenshot(const Options& options)
{
    gpu::ContextOptions contextOptions;
    contextOptions.applicationName = "Thesis Renderer (screenshot)";
    contextOptions.validation = options.validation;
    const gpu::Context context(contextOptions);

    const RenderedFrame frame = renderFrame(context, options);
    if (frame.log) {
        std::printf("Steps of the ray of pixel (%u, %u), ray %u:\n%s", options.inspect[0], options.inspect[1],
                    frame.log->ray, frame.logText.c_str());
    }

    const resolve::ShadingParams& shading = frame.shading;
    writeFigure(frame.image, shading, options.screenshot);
    const std::string file = utf8(options.screenshot);
    std::printf("%s: %s, tracer %s%s%s, view %s\n", file.c_str(), frame.sceneName.c_str(), frame.tracer.c_str(),
                frame.encoding.empty() ? "" : ", encoding ", frame.encoding.c_str(),
                resolve::kShadingModeNames.at(options.view - 1));
    if (resolve::showsCount(shading.mode)) {
        std::printf("colour scale: viridis, %s, 0 black, maximum %s, above it white\n", shading.workScale.kindName(),
                    resolve::formatCount(shading.workScale.maximum).c_str());
    } else if (shading.mode == resolve::ShadingMode::Distance) {
        std::printf("distance range: %g (white) to %g (black)\n", shading.distanceNear, shading.distanceFar);
    }

    if (options.validation && gpu::validationMessageCount() > 0) {
        std::fprintf(stderr, "%llu validation message(s) were reported.\n",
                     static_cast<unsigned long long>(gpu::validationMessageCount()));
        return 2;
    }
    return 0;
}

} // namespace thesis::app
