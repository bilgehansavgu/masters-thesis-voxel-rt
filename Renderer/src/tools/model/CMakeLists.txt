add_executable(thesis_model main.cpp)
thesis_target_defaults(thesis_model)
target_link_libraries(thesis_model PRIVATE
    thesis::mesh
    thesis::voxel
    thesis::encoding
    thesis::encoding_plugins
    thesis::metrics
    CLI11::CLI11)
set_target_properties(thesis_model PROPERTIES
    FOLDER "tools"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
