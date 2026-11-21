thesis_add_library(tracer_software_octree
    SOURCES
        OctreeDiagram.cpp
        Octree.cpp
        OctreeTracer.cpp
    PUBLIC_DEPS
        thesis::tracing
    FOLDER "tracers")
thesis_add_shaders(OWNER thesis_tracer_software_octree SOURCES shaders/trace_software_octree.slang
    PER_PRIMITIVE COUNTING)
thesis_register_plugin(SOCKET TRACER TARGET thesis_tracer_software_octree FUNCTION registerSoftwareOctreeTracer)
