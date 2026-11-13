thesis_add_library(resolve
    SOURCES HitShading.cpp
    PUBLIC_DEPS thesis::tracing)
thesis_add_shaders(OWNER thesis_resolve SOURCES shaders/shade.slang)
