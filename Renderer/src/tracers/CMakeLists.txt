# Tracer plugins. To add one: create a folder with its C++ and shader, add_subdirectory() it
# here, and call thesis_register_plugin(SOCKET TRACER ...) in its CMakeLists.txt.
add_subdirectory(software)
add_subdirectory(hardware)
add_subdirectory(null)

# thesis::tracer_plugins exposes registerBuiltInTracers(), generated from the registrations above.
thesis_add_library(tracer_plugins
    SOURCES
        include/thesis/tracers/BuiltInTracers.h
    PUBLIC_DEPS
        thesis::tracing
    FOLDER "tracers")
thesis_generate_plugin_registration(
    SOCKET TRACER
    TARGET thesis_tracer_plugins
    REGISTRY_HEADER "thesis/tracing/Tracer.h"
    REGISTRY_TYPE "tracing::TracerRegistry"
    ENTRY registerBuiltInTracers)
