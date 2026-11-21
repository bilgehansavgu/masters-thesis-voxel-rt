add_executable(thesis_renderer
    Benchmark.cpp
    Benchmark.h
    CommandLine.cpp
    CommandLine.h
    ControlPanel.cpp
    ControlPanel.h
    ControlPanelInspector.cpp
    ControlPanelMeasurements.cpp
    ControlPanelSettings.cpp
    ControlPanelStatusBar.cpp
    FramePipeline.cpp
    FramePipeline.h
    Gui.cpp
    Gui.h
    Interactive.cpp
    Interactive.h
    Letterbox.h
    Options.h
    Overlay.cpp
    Overlay.h
    PanelWidgets.cpp
    PanelWidgets.h
    Presenter.cpp
    Presenter.h
    RayLogText.cpp
    RayLogText.h
    StructureView.cpp
    StructureView.h
    RayLesson.cpp
    RayLesson.h
    RayWorkSummary.cpp
    RayWorkSummary.h
    SceneSetup.cpp
    SceneSetup.h
    Screenshot.cpp
    Screenshot.h
    Window.cpp
    Window.h
    WindowCommand.cpp
    WindowCommand.h
    main.cpp)
thesis_target_defaults(thesis_renderer)
target_link_libraries(thesis_renderer PRIVATE
    thesis::core
    thesis::gpu
    thesis::tracing
    thesis::workloads
    thesis::resolve
    thesis::metrics
    thesis::tracer_plugins
    thesis::voxel
    thesis::encoding
    thesis::encoding_plugins
    thesis::profiling
    glfw
    imgui::imgui
    CLI11::CLI11)
target_include_directories(thesis_renderer SYSTEM PRIVATE ${Stb_INCLUDE_DIR})
target_compile_definitions(thesis_renderer PRIVATE THESIS_DEFAULT_SHADER_DIR="${THESIS_SHADER_OUTPUT_DIR}")
target_compile_definitions(thesis_renderer PRIVATE
    THESIS_LUCY_MODEL="${CMAKE_SOURCE_DIR}/../Models/lucy-2048.voxels"
    THESIS_MODEL_DIR="${CMAKE_SOURCE_DIR}/../Models")
set_target_properties(thesis_renderer PROPERTIES
    FOLDER "app"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
