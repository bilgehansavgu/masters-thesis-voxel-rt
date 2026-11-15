# Thesis Renderer

Testbed for ray tracing compressed voxel data on GPUs with ray tracing hardware: the GPU's own
search structure (hardware tracer) against our own (software tracer), behind one interface.

## The idea in one picture

```
OFFLINE, once per model                                   (thesis_model voxelize)
  mesh file (PLY) → voxelizer → voxel file (.voxels)

WHEN A MODEL IS LOADED, and again when the encoding is switched
  voxel file → [ ENCODING SOCKET ] → buffers on the GPU, one primitive per brick
                 ├─ brick2012_inline   2012 format, attributes next to the heights (the paper)
                 ├─ brick2012_split    2012 format, attributes in their own buffer
                 └─ (later)            a baseline format

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

The encoding socket has one rule: **decoding the encoded data gives back the input voxels
exactly.** A test checks this for every encoding, and `thesis_model report` checks it for a
whole model.

State after step 1: the voxel data is built, encoded and on the GPU. The brick shaders still
stop at the box around each brick, so the image shows bricks, not voxels. Step 2 adds the test
inside a brick.

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
  shaders both include. The bits of the brick format are written once, in
  `shared/include/thesis/shared/Brick2012.h`.
- **Shader interfaces are checked.** The shader compiler writes a layout report (`<shader>.json`)
  next to each `.spv`. Creating a kernel fails with a clear message if a shader reads a binding
  the C++ side does not provide, with the wrong type, or expects a different push-constant
  size; `TestShaderLayouts` compares Ray, Hit and Aabb field by field.

## Making a voxel model

Models and voxel files are large and stay out of git. Put them in `Thesis/Models/` (ignored).

```bash
# mesh → voxel file. Lucy has z as its up axis; the renderer expects y.
build/src/tools/model/Release/thesis_model.exe voxelize --mesh ../Models/lucy/lucy.ply --up-axis z --resolution 2048 --output ../Models/lucy-2048.voxels

# what each encoding makes of it: size of every buffer, cell statistics, round trip check
build/src/tools/model/Release/thesis_model.exe report --model ../Models/lucy-2048.voxels
```

`--resolution` is the number of voxels along the longest side of the mesh. `--rule sampled`
(default) is the method of the paper; `--rule conservative` fills every voxel a triangle touches.
`report` writes `results/<time>_model_<name>.json` with all numbers and the git commit.

## Running

```bash
build/src/app/Release/thesis_renderer.exe                                       # box field (step 0 scene)
build/src/app/Release/thesis_renderer.exe --model ../Models/lucy-2048.voxels    # a voxel model
build/src/app/Release/thesis_renderer.exe --benchmark --model ../Models/lucy-2048.voxels --encoding brick2012_split
build/src/app/Release/thesis_renderer.exe --help
```

In the window: drag to orbit, scroll to zoom, **Tab** switches the tracer, **E** switches the
encoding, **1-4** switch the view. The panel shows the size of every buffer of the current
encoding and the bits per voxel. Switching the encoding encodes the model again and rebuilds
the search structures, so the numbers in the panel always belong to what is on screen.

`--benchmark` is headless and writes `results/<time>_<tracer>[_<encoding>].json`.
`--list-tracers` and `--list-encodings` print what is built in.

The software tracer still tests every primitive for every ray. It refuses scenes with more than
30,000 primitives, because one frame would take longer than Windows allows a GPU job.

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
   state, Windows power plan and mode, build configuration and git commit. For a voxel model
   it also records the encoding, the size of every buffer and the statistics of the encoding.
4. Validation must be off for numbers (`--validation` is for correctness only).
5. Rays and hits pass through GPU memory. That costs every tracer the same; the `null` tracer
   measures exactly that floor, and the benchmark prints each tracer's time above it.

For thesis numbers: plugged in, same Windows power mode, nothing else running.

## Layout

| Folder | Job | May use |
|---|---|---|
| `src/core` | Data types, scene, camera, statistics, registry | nothing (glm) |
| `src/mesh` | Triangle mesh, PLY reader | miniply |
| `src/voxel` | Voxel model, voxelizer, voxel file, packed normals | mesh |
| `src/encoding` | The encoding socket: `IEncoding`, memory report | core, voxel |
| `src/encodings/*` | One folder per encoding plugin (`brick2012`: both layouts) | encoding |
| `src/gpu` | Thin Vulkan layer: device, buffers, kernels, timers | Vulkan, vk-bootstrap, VMA, Tracy |
| `src/tracing` | The tracer socket: `ITracer`, ray and hit lists, GPU scene | core, gpu |
| `src/tracers/*` | One folder per tracer plugin, with its shader | tracing |
| `src/workloads` | Ray generators (camera rays) | tracing |
| `src/resolve` | Hits → image | tracing |
| `src/metrics` | Measurement output socket: Tracy plots, JSON results, system state | core |
| `src/app` | Window, control panel, presenter, benchmark mode, frame pipeline, scene source | everything |
| `src/tools/model` | `thesis_model`: voxelize a mesh, report on the encodings | encodings, metrics |
| `shared` | Headers read by both C++ and shaders (binding slots, brick bit layout) | |
| `shaders/common` | Shared GPU code (`types.slang`, `aabb.slang`) | |
| `shaders/primitives/<kind>` | What is inside a primitive's box: `box`, `brick2012_inline`, `brick2012_split` | |
| `tests` | Unit tests, encoding round trips, the cross-tracer agreement test | |

Each library only sees the headers of libraries it links, so the dependency direction is
enforced by the build. Mesh, voxel and encoding code knows nothing about Vulkan.

## Extending

- **New tracer:** add `src/tracers/<name>/` with an `ITracer` implementation, its shader, and a
  `CMakeLists.txt` calling `thesis_register_plugin(SOCKET TRACER ...)`; add the folder in
  `src/tracers/CMakeLists.txt`. No other code changes; the agreement test covers it automatically.
- **New encoding:** add `src/encodings/<name>/` with an `IEncoding` implementation and a
  `CMakeLists.txt` calling `thesis_register_plugin(SOCKET ENCODING ...)`; add the folder in
  `src/encodings/CMakeLists.txt`. Add a primitive kind with the same name (next point). The
  round trip test, the report tool, the benchmark and the panel pick it up automatically.
- **New primitive kind:** add `shaders/primitives/<kind>/primitive.slang` implementing the
  contract described in `shaders/primitives/box/primitive.slang`, and add the kind to
  `THESIS_PRIMITIVE_KINDS` in `CMakeLists.txt`. Every tracer is compiled for it automatically.
  The kind's own buffers go in `core::Scene::primitiveData` (bindings 9..15); tracers pass them
  through without knowing about them.
- **Baseline tracers** (like `null`) return `false` from `ITracer::findsHits()`: they are
  benchmarked, but skipped by the correctness tests.
- **A tracer that cannot handle a scene** throws `tracing::UnsupportedScene` from `prepare()`.
  The window leaves it out and says why; the benchmark skips it unless it was asked for by name.
