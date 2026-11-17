thesis_add_library(tracer_software_grid
    SOURCES
        GridTracer.cpp
        UniformGrid.cpp
    PUBLIC_DEPS thesis::tracing
    FOLDER "tracers")
thesis_add_shaders(OWNER thesis_tracer_software_grid SOURCES shaders/trace_software_grid.slang PER_PRIMITIVE COUNTING)
thesis_register_plugin(SOCKET TRACER TARGET thesis_tracer_software_grid FUNCTION registerSoftwareGridTracer)
