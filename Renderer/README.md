# Thesis Renderer

Testbed for ray tracing compressed voxel data on GPUs with ray tracing hardware: the GPU's own
search structure (hardware tracer) against our own (software tracer), behind one interface.

## The idea in one picture

```
OFFLINE, once per model                       (step 1)
  mesh file → voxelizer → bricks → encoder → scene file

EVERY FRAME
  camera rays → [ TRACER SOCKET ] → hits → shading → screen
                  ├─ hardware   Vulkan ray queries walk a GPU-built BVH
                  ├─ software   compute shader, our own search structure
                  ├─ null       baseline: moves rays and hits, searches nothing
                  └─ (later)    OptiX, ray tracing pipeline, ...

  every stage is timed on the GPU; Tracy shows it live, results files keep it
```

The tracer socket has one rule: **a list of rays goes in, a list of hits comes out.** All
tracers get identical rays. Automatic tests check that they return identical hits (distance,
primitive and side) and that they match hand-computed answers.

## Building

Needs: Visual Studio 2022, CMake 3.28+, the Vulkan SDK (for `slangc` and the validation layer),
vcpkg with the `VCPKG_ROOT` environment variable set, and a GPU with hardware ray tracing.

```bash
cmake --preset vs2022
cmake --build --preset release
ctest --preset release
```

The first configure builds the dependencies through vcpkg (`vcpkg.json`), which takes a few minutes.

Code rules the build enforces:

- **Warnings are errors** in project code (`-DTHESIS_WARNINGS_AS_ERRORS=OFF` to experiment).
- **Formatting** follows `.clang-format`. The `format_check` test fails on unformatted code;
  `cmake --build --preset release --target format` fixes it.
- **Binding slots** are written once, in `shared/include/thesis/shared/Bindings.h`, which C++ and
  shaders both include.
- **Shader interfaces are checked.** The shader compiler writes a layout report (`<shader>.json`)
  next to each `.spv`. Creating a kernel fails with a clear message if a shader reads a binding
  the C++ side does not provide, with the wrong type, or expects a different push-constant
  size; `TestShaderLayouts` compares Ray, Hit and Aabb field by field.

## Running

```bash
build/src/app/Release/thesis_renderer.exe                  # window: drag to orbit, Tab switches tracer, 1-4 views
build/src/app/Release/thesis_renderer.exe --benchmark      # headless; writes results/<time>_<tracer>.json
build/src/app/Release/thesis_renderer.exe --help
```

Live profiling: start the [Tracy](https://github.com/wolfpld/tracy/releases) profiler
**v0.11.1** (must match the client version in vcpkg) and connect while the renderer runs.

## Measuring rules

The benchmark follows a fixed protocol so numbers can be compared across days:

1. Warm-up runs for at least `--warmup` seconds (default 3) and until the GPU clock has
   settled, at most `--warmup-max` seconds (default 20). The table's "settled" column says
   whether it did; the system state is captured after warm-up.
2. Several repeats of one camera orbit; reported value = median of the repeat medians.
   The GPU clock is also recorded after each repeat.
3. Each results file records GPU, driver, clocks, temperature, power draw and limit, charger
   state, Windows power plan and mode, build configuration and git commit.
4. Validation must be off for numbers (`--validation` is for correctness only).
5. Rays and hits pass through GPU memory. That costs every tracer the same; the `null` tracer
   measures exactly that floor, and the benchmark prints each tracer's time above it.

For thesis numbers: plugged in, same Windows power mode, nothing else running.

## Layout

| Folder | Job | May use |
|---|---|---|
| `src/core` | Data types, scene, camera, statistics, registry | nothing (glm) |
| `src/gpu` | Thin Vulkan layer: device, buffers, kernels, timers | Vulkan, vk-bootstrap, VMA, Tracy |
| `src/tracing` | The tracer socket: `ITracer`, ray and hit lists, GPU scene | core, gpu |
| `src/tracers/*` | One folder per tracer plugin, with its shader | tracing |
| `src/workloads` | Ray generators (camera rays) | tracing |
| `src/resolve` | Hits → image | tracing |
| `src/metrics` | Measurement output socket: Tracy plots, JSON results, system state | core |
| `src/app` | Window, control panel, presenter, benchmark mode, frame pipeline | everything |
| `shared` | Headers read by both C++ and shaders (binding slots) | |
| `shaders/common` | Shared GPU formats (`types.slang`) | |
| `shaders/primitives/<kind>` | What is inside a primitive's box (`box` now, brick encoding later) | |
| `tests` | Unit tests and the cross-tracer agreement test | |

Each library only sees the headers of libraries it links, so the dependency direction is
enforced by the build.

## Extending

- **New tracer:** add `src/tracers/<name>/` with an `ITracer` implementation, its shader, and a
  `CMakeLists.txt` calling `thesis_register_plugin(SOCKET TRACER ...)`; add the folder in
  `src/tracers/CMakeLists.txt`. No other code changes; the agreement test covers it automatically.
- **New primitive kind** (e.g. the 2012 brick encoding): add `shaders/primitives/<kind>/primitive.slang`
  implementing the contract described in `shaders/primitives/box/primitive.slang`, and add the
  kind to `THESIS_PRIMITIVE_KINDS`. Every tracer is compiled for it automatically. The kind's
  own buffers go in `core::Scene::primitiveData` (bindings 9..15); tracers pass them through
  without knowing about them.
- **Baseline tracers** (like `null`) return `false` from `ITracer::findsHits()`: they are
  benchmarked, but skipped by the correctness tests.
