thesis_add_library(mesh
    SOURCES
        GltfLoader.cpp
        MeshFile.cpp
        MeshShapes.cpp
        ObjLoader.cpp
        PlyLoader.cpp
        PlyWriter.cpp
    PUBLIC_DEPS
        glm::glm
    PRIVATE_DEPS
        unofficial::miniply::miniply
        tinyobjloader::tinyobjloader)
target_include_directories(thesis_mesh SYSTEM PRIVATE ${CGLTF_INCLUDE_DIR})
