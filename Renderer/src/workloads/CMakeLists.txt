thesis_add_library(workloads
    SOURCES CameraRays.cpp
    PUBLIC_DEPS thesis::tracing)
thesis_add_shaders(OWNER thesis_workloads SOURCES shaders/camera_rays.slang)
