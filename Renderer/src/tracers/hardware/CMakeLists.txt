thesis_add_library(tracer_hardware
    SOURCES HardwareTracer.cpp
    PUBLIC_DEPS thesis::tracing
    FOLDER "tracers")
thesis_add_shaders(OWNER thesis_tracer_hardware SOURCES shaders/trace_hardware.slang PER_PRIMITIVE COUNTING
    CAPABILITIES spvRayQueryKHR)
thesis_register_plugin(SOCKET TRACER TARGET thesis_tracer_hardware FUNCTION registerHardwareTracer)
