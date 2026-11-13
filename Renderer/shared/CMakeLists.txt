# Headers read by both C++ and Slang (preprocessor-only content).
add_library(thesis_shared INTERFACE)
add_library(thesis::shared ALIAS thesis_shared)
target_include_directories(thesis_shared INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(THESIS_SHARED_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include" CACHE INTERNAL "")
