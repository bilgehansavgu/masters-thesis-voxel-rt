# architecture_check: the rules of ARCHITECTURE.md that a script can check (plugin names stay in
# their own folders, every header says its side). See scripts/check_architecture.py.

find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_test(NAME architecture_check
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/check_architecture.py"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
