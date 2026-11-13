thesis_add_library(metrics
    SOURCES
        BuildInfo.cpp
        JsonReportSink.cpp
        MetricFanout.cpp
        SystemInfo.cpp
        TracySink.cpp
    PUBLIC_DEPS
        thesis::core
        nlohmann_json::nlohmann_json
    PRIVATE_DEPS
        thesis::profiling)

if(WIN32)
    target_link_libraries(thesis_metrics PRIVATE PowrProf)
endif()

# The git commit is captured at build time (not configure time), so results never name stale code.
set(git_info_header "${CMAKE_CURRENT_BINARY_DIR}/generated/thesis/GitInfo.h")
add_custom_target(thesis_git_info
    COMMAND "${CMAKE_COMMAND}" -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DOUTPUT=${git_info_header}
            -P "${CMAKE_SOURCE_DIR}/cmake/GitInfo.cmake"
    BYPRODUCTS "${git_info_header}"
    COMMENT "Recording git commit for results files"
    VERBATIM)
set_target_properties(thesis_git_info PROPERTIES FOLDER "generated")
add_dependencies(thesis_metrics thesis_git_info)
target_include_directories(thesis_metrics PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_definitions(thesis_metrics PRIVATE THESIS_BUILD_CONFIG="$<CONFIG>")
