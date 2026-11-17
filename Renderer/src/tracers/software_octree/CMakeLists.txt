thesis_add_library(tracer_software_octree
    SOURCES
        Octree.cpp
        OctreeTracer.cpp
    PUBLIC_DEPS
        thesis::tracing
        thesis::tracer_software_grid # the octree stands on the cells of the grid frame
    FOLDER "tracers")
thesis_add_shaders(OWNER thesis_tracer_software_octree SOURCES shaders/trace_software_octree.slang
    PER_PRIMITIVE COUNTING)
thesis_register_plugin(SOCKET TRACER TARGET thesis_tracer_software_octree FUNCTION registerSoftwareOctreeTracer)
