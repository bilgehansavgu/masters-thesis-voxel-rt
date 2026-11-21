# Slang shader compilation.
#
# Shaders are compiled to SPIR-V at build time into ${THESIS_SHADER_OUTPUT_DIR}. Every shader can
# import the shared modules in shaders/common/. Tracer shaders are compiled once per primitive
# kind: the kind's folder is put on the search path, so `import primitive;` resolves to that kind.
# This is how an encoding plugs into every tracer without the tracer's code changing.
# The include/ folder of the calling library is on the search path too, so a plugin keeps the
# numbers its C++ and its shader share in its own folder (<Name>Shared.h), not in shared/.

find_program(SLANGC_EXECUTABLE slangc
    HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin"
    REQUIRED)

set(THESIS_SHADER_ROOT "${CMAKE_SOURCE_DIR}/shaders")
set(THESIS_SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders")
option(THESIS_SHADER_DEBUG_INFO "Embed debug info in SPIR-V (for Nsight shader debugging)" OFF)

# thesis_add_shaders(OWNER <target> SOURCES <file.slang>... [PER_PRIMITIVE] [COUNTING]
#                    [CAPABILITIES <cap>...])
#
# Compiles each source (entry point `main`, compute stage). Output name is the source's base name,
# or <base>.<kind> for every kind in THESIS_PRIMITIVE_KINDS when PER_PRIMITIVE is given.
# COUNTING compiles each output a second time as <name>.counting, with THESIS_COUNT_WORK defined:
# the variant that counts the work of each ray (shaders/common/work.slang). It is never timed.
# CAPABILITIES are extra Slang capabilities the shaders need (e.g. spvRayQueryKHR).
# A custom target <OWNER>_shaders is created in the calling directory and OWNER depends on it.
function(thesis_add_shaders)
    cmake_parse_arguments(ARG "PER_PRIMITIVE;COUNTING" "OWNER" "SOURCES;CAPABILITIES" ${ARGN})

    file(GLOB_RECURSE shared_modules CONFIGURE_DEPENDS
        "${THESIS_SHADER_ROOT}/*.slang" "${THESIS_SHARED_INCLUDE_DIR}/*.h")
    set(flags -target spirv -profile spirv_1_6 -entry main -stage compute -O2
              -warnings-as-errors all
              -I "${THESIS_SHADER_ROOT}/common"
              -I "${THESIS_SHARED_INCLUDE_DIR}")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include")
        file(GLOB_RECURSE own_headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h")
        list(APPEND shared_modules ${own_headers})
        list(APPEND flags -I "${CMAKE_CURRENT_SOURCE_DIR}/include")
    endif()
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
            set(variants "timed")
            if(ARG_COUNTING)
                list(APPEND variants "counting")
            endif()
            foreach(variant IN LISTS variants)
                if(variant STREQUAL "counting")
                    set(output "${stem}.counting")
                    set(variant_flags -D THESIS_COUNT_WORK)
                else()
                    set(output "${stem}")
                    set(variant_flags "")
                endif()
                # <output>.json is Slang's own report of the shader's resources and memory layouts.
                # ComputeKernel checks it against the C++ side when a kernel is created, and
                # tests/TestShaderLayouts.cpp checks the shared structs field by field.
                add_custom_command(
                    OUTPUT "${output}.spv" "${output}.json"
                    COMMAND "${CMAKE_COMMAND}" -E make_directory "${THESIS_SHADER_OUTPUT_DIR}"
                    COMMAND "${SLANGC_EXECUTABLE}" "${source_path}" ${flags} ${kind_flags} ${variant_flags}
                            -o "${output}.spv" -reflection-json "${output}.json"
                    DEPENDS "${source_path}" ${shared_modules}
                    COMMENT "slangc ${base} ${kind} ${variant}"
                    VERBATIM)
                list(APPEND outputs "${output}.spv" "${output}.json")
            endforeach()
        endforeach()
    endforeach()

    add_custom_target(${ARG_OWNER}_shaders DEPENDS ${outputs} SOURCES ${ARG_SOURCES})
    set_target_properties(${ARG_OWNER}_shaders PROPERTIES FOLDER "shaders")
    add_dependencies(${ARG_OWNER} ${ARG_OWNER}_shaders)
endfunction()
