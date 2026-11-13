thesis_add_library(tracer_software
    SOURCES SoftwareTracer.cpp
    PUBLIC_DEPS thesis::tracing
    FOLDER "tracers")
thesis_add_shaders(OWNER thesis_tracer_software SOURCES shaders/trace_software.slang PER_PRIMITIVE)
thesis_register_plugin(SOCKET TRACER TARGET thesis_tracer_software FUNCTION registerSoftwareTracer)
