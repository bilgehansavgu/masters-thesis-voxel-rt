add_executable(thesis_tests
    HitComparison.cpp
    HitComparison.h
    TestAgreement.cpp
    TestCore.cpp
    TestEncodings.cpp
    TestShaderLayouts.cpp
    TestVoxel.cpp
    VoxelTestHelpers.cpp
    VoxelTestHelpers.h)
thesis_target_defaults(thesis_tests)
target_link_libraries(thesis_tests PRIVATE
    thesis::core
    thesis::gpu
    thesis::tracing
    thesis::workloads
    thesis::tracer_plugins
    thesis::mesh
    thesis::voxel
    thesis::encoding
    thesis::encoding_plugins
    thesis::encoding_brick2012
    nlohmann_json::nlohmann_json
    Catch2::Catch2WithMain)
target_compile_definitions(thesis_tests PRIVATE THESIS_DEFAULT_SHADER_DIR="${THESIS_SHADER_OUTPUT_DIR}")
set_target_properties(thesis_tests PROPERTIES FOLDER "tests")

include(Catch)
catch_discover_tests(thesis_tests DISCOVERY_MODE PRE_TEST)
