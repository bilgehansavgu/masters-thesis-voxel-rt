cmake_minimum_required(VERSION 3.28)

project(ThesisRenderer
    VERSION 0.1.0
    DESCRIPTION "Voxel ray tracing testbed: hardware vs software tracers behind one interface"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ThesisLibraries)
include(ThesisPlugins)
include(ThesisShaders)

# Primitive kinds the tracers are compiled for. Each kind is a folder in shaders/primitives/
# that implements the primitive contract (see shaders/primitives/box/primitive.slang).
set(THESIS_PRIMITIVE_KINDS box CACHE STRING "Primitive kinds every tracer shader is compiled for")

find_package(Vulkan REQUIRED)
find_package(vk-bootstrap CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(Tracy CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(CLI11 CONFIG REQUIRED)
find_package(Catch2 3 CONFIG REQUIRED)

enable_testing()
include(ThesisFormat)

add_subdirectory(shared)
add_subdirectory(src)
add_subdirectory(tests)
