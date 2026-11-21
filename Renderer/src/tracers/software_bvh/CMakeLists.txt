thesis_add_library(tracer_software_bvh
    SOURCES
        BvhDiagram.cpp
        Bvh.cpp
        BvhTracer.cpp
    PUBLIC_DEPS thesis::tracing
    FOLDER "tracers")
thesis_add_shaders(OWNER thesis_tracer_software_bvh SOURCES shaders/trace_software_bvh.slang PER_PRIMITIVE COUNTING)
thesis_register_plugin(SOCKET TRACER TARGET thesis_tracer_software_bvh FUNCTION registerSoftwareBvhTracer)
