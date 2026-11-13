add_executable(thesis_renderer
    Benchmark.cpp
    Benchmark.h
    ControlPanel.cpp
    ControlPanel.h
    FramePipeline.cpp
    FramePipeline.h
    Gui.cpp
    Gui.h
    Interactive.cpp
    Interactive.h
    Options.h
    Presenter.cpp
    Presenter.h
    SceneSetup.cpp
    SceneSetup.h
    Window.cpp
    Window.h
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
    thesis::profiling
    glfw
    imgui::imgui
    CLI11::CLI11)
target_compile_definitions(thesis_renderer PRIVATE THESIS_DEFAULT_SHADER_DIR="${THESIS_SHADER_OUTPUT_DIR}")
set_target_properties(thesis_renderer PROPERTIES
    FOLDER "app"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
