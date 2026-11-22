thesis_add_library(resolve
    SOURCES ColorScale.cpp HitShading.cpp LegendImage.cpp LegendPainter.cpp ViewLegend.cpp ViewSupport.cpp
    PUBLIC_DEPS thesis::tracing
    PRIVATE_DEPS imgui::imgui)
thesis_add_shaders(OWNER thesis_resolve SOURCES shaders/shade.slang shaders/shade_work.slang)
