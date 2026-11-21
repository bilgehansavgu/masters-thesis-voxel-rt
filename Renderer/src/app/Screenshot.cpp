#include "Screenshot.h"

#include "FramePipeline.h"
#include "Gui.h"
#include "RayLogText.h"
#include "SceneSetup.h"

#include "thesis/gpu/Context.h"
#include "thesis/gpu/Image.h"
#include "thesis/resolve/ColorBarImage.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <cstdio>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace thesis::app {

int runScreenshot(const Options& options)
{
    gpu::ContextOptions contextOptions;
    contextOptions.applicationName = "Thesis Renderer (screenshot)";
    contextOptions.validation = options.validation;
    const gpu::Context context(contextOptions);

    SceneSource source(options);
    const BuiltScene built = source.build();
    const tracing::GpuScene gpuScene = tracing::GpuScene::upload(context, built.scene);
    const tracing::TracerRegistry registry = makeTracerRegistry();
    const std::string tracerName =
        options.tracers.empty() ? registry.defaultName() : selectTracers(registry, options.tracers).front();
    const auto tracer = registry.create(tracerName, tracing::TracerEnvironment{context, options.shaderDirectory});
    tracer->prepare(gpuScene);

    FramePipeline pipeline(context, options.shaderDirectory, {options.width, options.height},
                           static_cast<tracing::RayOrder>(options.rayOrder));
    const core::OrbitCamera camera = startCamera(options, built.scene.bounds);
    resolve::ShadingParams shading;
    shading.mode = static_cast<resolve::ShadingMode>(options.view - 1);
    shading.distanceFar = 2.0f * camera.distance;
    shading.workScale.maximum = options.scaleMaximum;
    shading.workScale.kind = options.logScale ? resolve::ScaleKind::Log2 : resolve::ScaleKind::Linear;

    const bool inspect = options.inspect.size() == 2;
    if (inspect) {
        if (options.inspect[0] >= options.width || options.inspect[1] >= options.height) {
            throw std::invalid_argument("--inspect: the pixel is outside the frame");
        }
        if (!tracer->countsWork()) {
            throw std::invalid_argument("--inspect: tracer '" + tracerName + "' does not count work");
        }
        pipeline.setLoggedRay(pipeline.batch().rayOf(options.inspect[0], options.inspect[1]));
    }

    context.submitAndWait([&](VkCommandBuffer cmd) {
        pipeline.begin(cmd);
        pipeline.trace(cmd, *tracer, camera.frame(pipeline.aspectRatio()), resolve::showsWork(shading.mode) || inspect);
        pipeline.shade(cmd, shading);
    });
    (void)pipeline.collectTimings();

    if (const std::optional<tracing::RayLog> log = pipeline.rayLog()) {
        std::printf("Steps of the ray of pixel (%u, %u), ray %u:\n%s", options.inspect[0], options.inspect[1], log->ray,
                    rayLogAsText(describeRayLog(*log, [&](const core::RayLogEntry& step) {
                        return tracer->describeStructureStep(step);
                    })).c_str());
    }

    resolve::RgbaImage image{options.width, options.height, gpu::downloadImage(context, pipeline.output())};
    if (resolve::showsCount(shading.mode)) {
        const std::filesystem::path font =
            std::filesystem::is_regular_file(kInterfaceFontFile) ? kInterfaceFontFile : std::filesystem::path{};
        image = resolve::withColorBar(image, shading.workScale, resolve::countedQuantity(shading.mode), font);
    }
    const std::string file = options.screenshot.string();
    const int written = stbi_write_png(file.c_str(), static_cast<int>(image.width), static_cast<int>(image.height), 4,
                                       image.pixels.data(), static_cast<int>(image.width) * 4);
    if (written == 0) {
        throw std::runtime_error("Cannot write '" + file + "'");
    }
    std::printf("%s: %s, tracer %s%s%s, view %s\n", file.c_str(), built.scene.name.c_str(), tracerName.c_str(),
                built.encoding.empty() ? "" : ", encoding ", built.encoding.c_str(),
                resolve::kShadingModeNames.at(options.view - 1));
    if (resolve::showsCount(shading.mode)) {
        std::printf("colour scale: viridis, %s, 0 black, maximum %s, above it white\n", shading.workScale.kindName(),
                    resolve::formatCount(shading.workScale.maximum).c_str());
    }

    if (options.validation && gpu::validationMessageCount() > 0) {
        std::fprintf(stderr, "%llu validation message(s) were reported.\n",
                     static_cast<unsigned long long>(gpu::validationMessageCount()));
        return 2;
    }
    return 0;
}

} // namespace thesis::app
