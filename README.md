# Voxel ray tracing on RT GPUs (master's thesis)

This master's thesis tests how well the compact voxel format of Reichl et al. (2012), "Hybrid Sample-based Surface Rendering", works on a current GPU with ray tracing hardware. It compares the GPU's own BVH (hardware tracer) with a search structure written for voxels (software tracer).

- **[Wiki](https://github.com/bilgehansavgu/masters-thesis-voxel-rt/wiki)**: the knowledge base. It holds the thesis question, the roadmap, the concepts, the design decisions, the results and the papers.
- **[`Renderer/`](Renderer)**: the test bench (C++20, Vulkan, Slang). See [`Renderer/README.md`](Renderer/README.md) to build and run it. [`Renderer/ARCHITECTURE.md`](Renderer/ARCHITECTURE.md) holds the code structure and its rules.
