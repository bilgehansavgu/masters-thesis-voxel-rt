# Conventions shared by every target in the project.

# Warnings fail the build for our own code (third-party headers are included as external and
# do not warn). To build anyway while experimenting: configure with -DTHESIS_WARNINGS_AS_ERRORS=OFF.
option(THESIS_WARNINGS_AS_ERRORS "Treat compiler warnings in project code as errors" ON)

function(thesis_target_defaults target)
    target_compile_features(${target} PUBLIC cxx_std_20)
    set_target_properties(${target} PROPERTIES COMPILE_WARNING_AS_ERROR ${THESIS_WARNINGS_AS_ERRORS})
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus /MP)
        target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# thesis_add_library(<name> SOURCES ... [PUBLIC_DEPS ...] [PRIVATE_DEPS ...])
#
# Creates the static library thesis_<name> (alias thesis::<name>). Public headers live in
# <dir>/include/thesis/<name>/, so a library can only include headers of libraries it links.
# That is what keeps the dependency direction honest.
function(thesis_add_library name)
    cmake_parse_arguments(ARG "" "FOLDER" "SOURCES;PUBLIC_DEPS;PRIVATE_DEPS" ${ARGN})
    add_library(thesis_${name} STATIC ${ARG_SOURCES})
    add_library(thesis::${name} ALIAS thesis_${name})
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include")
        target_include_directories(thesis_${name} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
    endif()
    target_link_libraries(thesis_${name} PUBLIC ${ARG_PUBLIC_DEPS} PRIVATE ${ARG_PRIVATE_DEPS})
    thesis_target_defaults(thesis_${name})
    if(NOT ARG_FOLDER)
        set(ARG_FOLDER "libraries")
    endif()
    set_target_properties(thesis_${name} PROPERTIES FOLDER "${ARG_FOLDER}")
endfunction()
