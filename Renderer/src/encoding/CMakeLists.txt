thesis_add_library(encoding
    SOURCES
        Encoding.cpp
    PUBLIC_DEPS
        thesis::core
        thesis::voxel
        nlohmann_json::nlohmann_json)
