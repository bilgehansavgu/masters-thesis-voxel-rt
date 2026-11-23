# Slang shader compilation.
#
# Shaders are compiled to SPIR-V at build time into ${THESIS_SHADER_OUTPUT_DIR}. Every shader can
# import the shared modules in shaders/common/. Tracer shaders are compiled once per primitive
# kind: the kind's folder is put on the search path, so `import primitive;` resolves to that kind.
# This is how an encoding plugs into every tracer without the tracer's code changing.
# The include/ folder of the calling library is on the search path too, so a plugin keeps the
# numbers its C++ and its shader share in its own folder (<Name>Shared.h), not in shared/.
# A primitive kind's shader is compiled inside each tracer, away from its encoder, so the
# include/ folder of every encoding plugin is on the search path of PER_PRIMITIVE shaders.

find_program(SLANGC_EXECUTABLE slangc
    HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin"
    REQUIRED)

set(THESIS_SHADER_ROOT "${CMAKE_SOURCE_DIR}/shaders")
set(THESIS_SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders")
option(THESIS_SHADER_DEBUG_INFO "Embed debug info in SPIR-V (for Nsight shader debugging)" OFF)

# thesis_add_shaders(OWNER <target> SOURCES <file.slang>... [PER_PRIMITIVE] [COUNTING]
#                    [RAY_TRACING] [CAPABILITIES <cap>...])
#
# Compiles each source (entry point `main`, compute stage). With RAY_TRACING, every entry point
# marked [shader("...")] goes into one SPIR-V file instead, and gpu::RayTracingPipeline picks
# the ones it uses by name. Output name is the source's base name,
# or <base>.<kind> for every kind in THESIS_PRIMITIVE_KINDS when PER_PRIMITIVE is given.
# COUNTING compiles each output a second time as <name>.counting, with THESIS_COUNT_WORK defined:
# the variant that counts the work of each ray (shaders/common/work.slang). It is never timed.
# CAPABILITIES are extra Slang capabilities the shaders need (e.g. spvRayQueryKHR).
# A custom target <OWNER>_shaders is created in the calling directory and OWNER depends on it.
function(thesis_add_shaders)
    cmake_parse_arguments(ARG "PER_PRIMITIVE;COUNTING;RAY_TRACING" "OWNER" "SOURCES;CAPABILITIES" ${ARGN})

    file(GLOB_RECURSE shared_modules CONFIGURE_DEPENDS
        "${THESIS_SHADER_ROOT}/*.slang" "${THESIS_SHARED_INCLUDE_DIR}/*.h")
    set(flags -target spirv -profile spirv_1_6 -O2
              -warnings-as-errors all
              -I "${THESIS_SHADER_ROOT}/common"
              -I "${THESIS_SHARED_INCLUDE_DIR}")
    if(NOT ARG_RAY_TRACING)
        list(APPEND flags -entry main -stage compute)
    endif()
    # A library's own shaders/ folder may hold modules that several of its sources import.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
        file(GLOB own_modules CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.slang")
        list(APPEND shared_modules ${own_modules})
        list(APPEND flags -I "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include")
        file(GLOB_RECURSE own_headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h")
        list(APPEND shared_modules ${own_headers})
        list(APPEND flags -I "${CMAKE_CURRENT_SOURCE_DIR}/include")
    endif()
    if(ARG_PER_PRIMITIVE)
        get_property(encoding_includes GLOBAL PROPERTY THESIS_ENCODING_PLUGIN_INCLUDE_DIRS)
        if(NOT encoding_includes)
            message(FATAL_ERROR "thesis_add_shaders(PER_PRIMITIVE) in ${CMAKE_CURRENT_SOURCE_DIR}: no "
                                "encoding plugin is registered yet. Add src/encodings before the tracers.")
        endif()
        foreach(encoding_include IN LISTS encoding_includes)
            file(GLOB_RECURSE encoding_headers CONFIGURE_DEPENDS "${encoding_include}/*Shared.h")
            list(APPEND shared_modules ${encoding_headers})
            list(APPEND flags -I "${encoding_include}")
        endforeach()
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
                    # The counting variant also reads the GPU clock (cycles of each ray).
                    set(variant_flags -D THESIS_COUNT_WORK -capability spvShaderClockKHR)
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
