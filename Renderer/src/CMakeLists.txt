# Dependency direction (an arrow means "may use"):
#
#   app ──► tracers/* ──► tracing ──► gpu ──► profiling
#    │        workloads ─┘   │
#    │        resolve ───────┘
#    └──► metrics ──► core ◄── tracing
#
# core and metrics know nothing about Vulkan. Each library only sees the headers of what it links.

add_subdirectory(core)
add_subdirectory(profiling)
add_subdirectory(gpu)
add_subdirectory(tracing)
add_subdirectory(workloads)
add_subdirectory(resolve)
add_subdirectory(metrics)
add_subdirectory(tracers)
add_subdirectory(app)
