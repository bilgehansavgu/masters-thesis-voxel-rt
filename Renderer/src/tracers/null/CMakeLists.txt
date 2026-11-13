thesis_add_library(tracer_null
    SOURCES NullTracer.cpp
    PUBLIC_DEPS thesis::tracing
    FOLDER "tracers")
thesis_add_shaders(OWNER thesis_tracer_null SOURCES shaders/trace_null.slang)
thesis_register_plugin(SOCKET TRACER TARGET thesis_tracer_null FUNCTION registerNullTracer)
