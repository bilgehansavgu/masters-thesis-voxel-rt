# Slang shader compilation.
#
# Shaders are compiled to SPIR-V at build time into ${THESIS_SHADER_OUTPUT_DIR}. Every shader can
# import the shared modules in shaders/common/. Tracer shaders are compiled once per primitive
# kind: the kind's folder is put on the search path, so `import primitive;` resolves to that kind.
# This is how an encoding plugs into every tracer without the tracer's code changing.

find_program(SLANGC_EXECUTABLE slangc
    HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin"
    REQUIRED)

set(THESIS_SHADER_ROOT "${CMAKE_SOURCE_DIR}/shaders")
set(THESIS_SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders")
option(THESIS_SHADER_DEBUG_INFO "Embed debug info in SPIR-V (for Nsight shader debugging)" OFF)

# thesis_add_shaders(OWNER <target> SOURCES <file.slang>... [PER_PRIMITIVE] [CAPABILITIES <cap>...])
#
# Compiles each source (entry point `main`, compute stage). Output name is the source's base name,
# or <base>.<kind> for every kind in THESIS_PRIMITIVE_KINDS when PER_PRIMITIVE is given.
# CAPABILITIES are extra Slang capabilities the shaders need (e.g. spvRayQueryKHR).
# A custom target <OWNER>_shaders is created in the calling directory and OWNER depends on it.
function(thesis_add_shaders)
    cmake_parse_arguments(ARG "PER_PRIMITIVE" "OWNER" "SOURCES;CAPABILITIES" ${ARGN})

    file(GLOB_RECURSE shared_modules CONFIGURE_DEPENDS
        "${THESIS_SHADER_ROOT}/*.slang" "${THESIS_SHARED_INCLUDE_DIR}/*.h")
    set(flags -target spirv -profile spirv_1_6 -entry main -stage compute -O2
              -warnings-as-errors all
              -I "${THESIS_SHADER_ROOT}/common"
              -I "${THESIS_SHARED_INCLUDE_DIR}")
    foreach(capability IN LISTS ARG_CAPABILITIES)
        list(APPEND flags -capability ${capability})
    endforeach()
    if(THESIS_SHADER_DEBUG_INFO)
        list(APPEND flags -g2)
    endif()

    set(outputs "")
    foreach(source IN LISTS ARG_SOURCES)
        get_filename_component(source_path "${source}" ABSOLUTE)
        get_filename_component(base "${source}" NAME_WE)

        if(ARG_PER_PRIMITIVE)
            set(kinds ${THESIS_PRIMITIVE_KINDS})
        else()
            set(kinds "")
        endif()
        if(NOT kinds)
            set(kinds "-")
        endif()

        foreach(kind IN LISTS kinds)
            if(kind STREQUAL "-")
                set(stem "${THESIS_SHADER_OUTPUT_DIR}/${base}")
                set(kind_flags "")
            else()
                set(stem "${THESIS_SHADER_OUTPUT_DIR}/${base}.${kind}")
                set(kind_flags -I "${THESIS_SHADER_ROOT}/primitives/${kind}")
            endif()
            # <stem>.json is Slang's own report of the shader's resources and memory layouts.
            # ComputeKernel checks it against the C++ side when a kernel is created, and
            # tests/TestShaderLayouts.cpp checks the shared structs field by field.
            add_custom_command(
                OUTPUT "${stem}.spv" "${stem}.json"
                COMMAND "${CMAKE_COMMAND}" -E make_directory "${THESIS_SHADER_OUTPUT_DIR}"
                COMMAND "${SLANGC_EXECUTABLE}" "${source_path}" ${flags} ${kind_flags}
                        -o "${stem}.spv" -reflection-json "${stem}.json"
                DEPENDS "${source_path}" ${shared_modules}
                COMMENT "slangc ${base} ${kind}"
                VERBATIM)
            list(APPEND outputs "${stem}.spv" "${stem}.json")
        endforeach()
    endforeach()

    add_custom_target(${ARG_OWNER}_shaders DEPENDS ${outputs} SOURCES ${ARG_SOURCES})
    set_target_properties(${ARG_OWNER}_shaders PROPERTIES FOLDER "shaders")
    add_dependencies(${ARG_OWNER} ${ARG_OWNER}_shaders)
endfunction()
