thesis_add_library(tracer_hardware_pipeline
    SOURCES
        BoxOverlap.cpp
        HardwarePipelineTracer.cpp
    PUBLIC_DEPS thesis::tracing
    FOLDER "tracers")
thesis_add_shaders(OWNER thesis_tracer_hardware_pipeline
    SOURCES shaders/trace_pipeline.slang shaders/trace_pipeline_reorder.slang
    PER_PRIMITIVE COUNTING RAY_TRACING
    CAPABILITIES spvShaderInvocationReorderNV) # only the file that reorders uses it, see that file
thesis_register_plugin(SOCKET TRACER TARGET thesis_tracer_hardware_pipeline FUNCTION registerHardwarePipelineTracers)
