#include "Screenshot.h"

#include "FramePipeline.h"
#include "SceneSetup.h"

#include "thesis/gpu/Context.h"
#include "thesis/gpu/Image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <cstdio>
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

    const SceneSource source(options);
    const BuiltScene built = source.build(source.initialEncoding());
    const tracing::GpuScene gpuScene = tracing::GpuScene::upload(context, built.scene);
    const tracing::TracerRegistry registry = makeTracerRegistry();
    const std::string tracerName =
        options.tracers.empty() ? "hardware" : selectTracers(registry, options.tracers).front();
    const auto tracer = registry.create(tracerName, tracing::TracerEnvironment{context, options.shaderDirectory});
    tracer->prepare(gpuScene);

    FramePipeline pipeline(context, options.shaderDirectory, {options.width, options.height});
    const core::OrbitCamera camera = startCamera(options, built.scene.bounds);
    resolve::ShadingParams shading;
    shading.mode = static_cast<resolve::ShadingMode>(options.view - 1);
    shading.distanceFar = 2.0f * camera.distance;

    context.submitAndWait([&](VkCommandBuffer cmd) {
        pipeline.begin(cmd);
        pipeline.trace(cmd, *tracer, camera.frame(pipeline.aspectRatio()), resolve::showsWork(shading.mode));
        pipeline.shade(cmd, shading);
    });
    (void)pipeline.collectTimings();

    const std::vector<std::byte> pixels = gpu::downloadImage(context, pipeline.output());
    const std::string file = options.screenshot.string();
    const int written = stbi_write_png(file.c_str(), static_cast<int>(options.width), static_cast<int>(options.height),
                                       4, pixels.data(), static_cast<int>(options.width) * 4);
    if (written == 0) {
        throw std::runtime_error("Cannot write '" + file + "'");
    }
    std::printf("%s: %s, tracer %s%s%s, view %s\n", file.c_str(), built.scene.name.c_str(), tracerName.c_str(),
                built.encoding.empty() ? "" : ", encoding ", built.encoding.c_str(),
                resolve::kShadingModeNames.at(options.view - 1));

    if (options.validation && gpu::validationMessageCount() > 0) {
        std::fprintf(stderr, "%llu validation message(s) were reported.\n",
                     static_cast<unsigned long long>(gpu::validationMessageCount()));
        return 2;
    }
    return 0;
}

} // namespace thesis::app
