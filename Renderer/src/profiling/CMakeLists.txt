# Header-only wrapper around Tracy, so the rest of the code names one profiling API.
add_library(thesis_profiling INTERFACE)
add_library(thesis::profiling ALIAS thesis_profiling)
target_include_directories(thesis_profiling INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(thesis_profiling INTERFACE Tracy::TracyClient)
