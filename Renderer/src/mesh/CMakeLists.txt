thesis_add_library(mesh
    SOURCES
        MeshShapes.cpp
        PlyLoader.cpp
    PUBLIC_DEPS
        glm::glm
    PRIVATE_DEPS
        unofficial::miniply::miniply)
