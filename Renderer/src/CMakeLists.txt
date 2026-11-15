# Dependency direction (an arrow means "may use"):
#
#   app ──► tracers/* ──► tracing ──► gpu ──► profiling
#    │        workloads ─┘   │
#    │        resolve ───────┘
#    ├──► metrics ──► core ◄── tracing
#    └──► encodings/* ──► encoding ──► voxel ──► mesh
#                            └──► core
#   tools/model ──► encodings/*, metrics
#
# core, metrics, mesh, voxel and the encodings know nothing about Vulkan: voxel data is made and
# checked on the CPU. Each library only sees the headers of what it links.

add_subdirectory(core)
add_subdirectory(mesh)
add_subdirectory(voxel)
add_subdirectory(encoding)
add_subdirectory(encodings)
add_subdirectory(profiling)
add_subdirectory(gpu)
add_subdirectory(tracing)
add_subdirectory(workloads)
add_subdirectory(resolve)
add_subdirectory(metrics)
add_subdirectory(tracers)
add_subdirectory(app)
add_subdirectory(tools/model)
