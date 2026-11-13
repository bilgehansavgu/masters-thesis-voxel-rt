thesis_add_library(tracing
    SOURCES
        GpuScene.cpp
        RayBatch.cpp
    PUBLIC_DEPS
        thesis::core
        thesis::gpu
        thesis::shared)
