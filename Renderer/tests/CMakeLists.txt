add_executable(thesis_tests
    CubeByCubeTracer.cpp
    CubeByCubeTracer.h
    GpuTestHelpers.cpp
    GpuTestHelpers.h
    HitComparison.cpp
    HitComparison.h
    TestAgreement.cpp
    TestBrickTracing.cpp
    TestColorScale.cpp
    TestCore.cpp
    TestEncodings.cpp
    TestNormals.cpp
    TestRayOrder.cpp
    TestSearchStructures.cpp
    TestShaderLayouts.cpp
    TestViewLegend.cpp
    TestVoxel.cpp
    TestWorkSummary.cpp
    VoxelTestHelpers.cpp
    VoxelTestHelpers.h)
thesis_target_defaults(thesis_tests)
target_link_libraries(thesis_tests PRIVATE
    thesis::core
    thesis::gpu
    thesis::tracing
    thesis::workloads
    thesis::resolve
    thesis::tracer_plugins
    thesis::tracer_hardware_pipeline
    thesis::tracer_software_grid
    thesis::tracer_software_octree
    thesis::tracer_software_bvh
    thesis::mesh
    thesis::voxel
    thesis::encoding
    thesis::encoding_plugins
    thesis::encoding_brick2012
    thesis::encoding_bitmask
    thesis::metrics
    nlohmann_json::nlohmann_json
    Catch2::Catch2WithMain)
target_compile_definitions(thesis_tests PRIVATE THESIS_DEFAULT_SHADER_DIR="${THESIS_SHADER_OUTPUT_DIR}")
set_target_properties(thesis_tests PROPERTIES FOLDER "tests")
# Shaders that only the tests run.
# The subgroup capabilities are for warp_lanes.slang (one call per owner, so they apply to both).
thesis_add_shaders(OWNER thesis_tests SOURCES shaders/decode_normals.slang shaders/warp_lanes.slang
    CAPABILITIES spvGroupNonUniform spvGroupNonUniformArithmetic)

include(Catch)
catch_discover_tests(thesis_tests DISCOVERY_MODE PRE_TEST)
