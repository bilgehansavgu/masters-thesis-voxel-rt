# C++ formatting with clang-format (style in .clang-format).
#   build target `format`        reformats every C++ file in place
#   test         `format_check`  fails if any file is not formatted (part of ctest)
# Visual Studio ships clang-format with its LLVM component, so no extra install is needed.

find_program(CLANG_FORMAT_EXECUTABLE clang-format
    HINTS
        "$ENV{VSINSTALLDIR}/VC/Tools/Llvm/x64/bin"
        "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/x64/bin"
        "C:/Program Files/Microsoft Visual Studio/2022/Professional/VC/Tools/Llvm/x64/bin"
        "C:/Program Files/Microsoft Visual Studio/2022/Enterprise/VC/Tools/Llvm/x64/bin")

if(NOT CLANG_FORMAT_EXECUTABLE)
    message(STATUS "clang-format not found: the format target and format_check test are disabled")
    return()
endif()

file(GLOB_RECURSE THESIS_FORMATTED_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*.cpp" "${CMAKE_SOURCE_DIR}/src/*.h"
    "${CMAKE_SOURCE_DIR}/tests/*.cpp" "${CMAKE_SOURCE_DIR}/tests/*.h")

add_custom_target(format
    COMMAND "${CLANG_FORMAT_EXECUTABLE}" -i --style=file ${THESIS_FORMATTED_SOURCES}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Formatting C++ sources"
    VERBATIM)
set_target_properties(format PROPERTIES FOLDER "tools")

add_test(NAME format_check
    COMMAND "${CLANG_FORMAT_EXECUTABLE}" --dry-run --Werror --style=file ${THESIS_FORMATTED_SOURCES}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
