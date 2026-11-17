# Thesis Renderer

Testbed for ray tracing compressed voxel data on GPUs with ray tracing hardware: the GPU's own
search structure (hardware tracer) against our own (software tracers: grid, octree, BVH),
behind one interface.

## The idea in one picture

```
OFFLINE, once per model                                   (thesis_model voxelize)
  mesh file (PLY) → voxelizer → voxel file (.voxels)

WHEN A MODEL IS LOADED, and again when the encoding is switched
  voxel file → (bricks of 8, 16 or 32 voxels) → [ ENCODING SOCKET ] → buffers on the GPU, one primitive per brick
                 ├─ brick2012_inline       2012 format, attributes next to the heights (the paper)
                 ├─ brick2012_split        2012 format, attributes in their own buffer
                 ├─ brick2012_inline_mip   the same with the min/max mipmap of each brick
                 ├─ brick2012_split_mip    the same with the min/max mipmap of each brick
                 └─ (later)                a baseline format

EVERY FRAME
  camera rays → [ TRACER SOCKET ] → hits → shading → screen
                  ├─ hardware          Vulkan ray queries walk a GPU-built BVH
                  ├─ software_grid     compute shader, walks our own uniform grid
                  ├─ software_octree   compute shader, walks our own sparse octree
                  ├─ software_bvh      compute shader, walks our own binary BVH (SAH build on the CPU)
                  ├─ null              baseline: moves rays and hits, searches nothing
                  └─ (later)           OptiX, ray tracing pipeline, ...

  every stage is timed on the GPU; Tracy shows it live, results files keep it
```

The tracer socket has one rule: **a list of rays goes in, a list of hits comes out.** All
tracers get identical rays. Automatic tests check that they return identical hits (distance,
primitive, side and stored normal), that the hits match hand-computed answers, and that they
match a test of each ray against the cube of every voxel on the CPU.

The encoding socket has one rule: **decoding the encoded data gives back the input voxels
exactly.** A test checks this for every encoding, and `thesis_model report` checks it for a
whole model.

State after step 3: every tracer shows a voxel model. Every tracer runs the same brick test for
each brick that its search structure gives it: the ray walks the primary grid of the brick and
tests the filled voxels of each column (`shaders/common/brick2012.slang`). With a `_mip`
encoding the walk first reads the min/max mipmap of the brick and skips every square of cells
that the ray passes above or below. It finds the same hits, bit for bit. The software tracers
walk a uniform grid, a sparse octree or a binary BVH of their own. The window shows what each
ray does: work views, live averages, and the ray inspector, which lists every step of one ray.

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
  size; `TestShaderLayouts` compares Ray, Hit, RayWork, Aabb and the structs of the brick
  buffers field by field.

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
build/src/app/Release/thesis_renderer.exe --model ../Models/lucy-2048.voxels --screenshot lucy.png --camera 90 10 0.75
build/src/app/Release/thesis_renderer.exe --model ../Models/lucy-2048.voxels --encoding brick2012_inline_mip --tracer software_octree --screenshot ray.png --inspect 960 540
build/src/app/Release/thesis_renderer.exe --help
```

In the window: drag to orbit, scroll to zoom. **Tab** switches the tracer, **E** the encoding,
**B** the brick size (with Shift: the one before), **1-9, 0, -, =** the view. **M** measures
every tracer at the current camera ("Measure all"), **R** resets the camera. A **right click**
on the image starts the ray inspector on that pixel, **Esc** stops it. The panel shows the size
of every buffer of the current encoding and the bits per voxel. Switching the encoding or the
brick size encodes the model again and rebuilds the search structures, so the numbers in the
panel always belong to what is on screen.

| Key | View |
|---|---|
| 1 | Lit with the stored normal of the voxel (a box has none and gets the normal of its face) |
| 2 | Distance |
| 3 | Primitive id (for a voxel model: one colour per brick) |
| 4 | Stored normal as colour |
| 5 | Lit with the face of the voxel cube: shows the voxels |
| 6, 7, 8 | Work of each ray: cells visited, primitives tested, runs tested |
| 9 | Kind of the cell at the hit: green one voxel, blue one run, red several runs |
| 0 | Steps through the tracer's own search structure (the hardware tracer cannot count them) |
| - | Mipmap cells read |
| = | The largest mipmap square that the walk skipped: blue 2x2 cells, green 4x4, yellow 8x8, orange 16x16, red 32x32 |

The views 6 to = and the ray inspector run the counting variant of the tracer's shader (see
below), which is slower. The trace time in the panel is then not the time of the tracer. With a
work view on, the panel also shows the average work of a ray in the frame, about once a second.

The **ray inspector** lists every step of one ray in order: the cells or nodes of the search
structure, the primitives it was tested against, and inside each brick the mipmap squares (with
their lowest and highest voxel, and whether the ray skipped them), the cells and the runs, then
the hit. "Copy as text" puts the list on the clipboard. The hardware tracer cannot report the
nodes of its BVH, so its list starts at the primitives.

`--benchmark` is headless and writes `results/<time>_<tracer>[_<encoding>].json`.
`--screenshot <file.png>` is headless too: it writes one frame, with `--view <1-12>`. With
`--inspect <x> <y>` it also prints every step of the ray of that pixel. In the window,
`--inspect` starts the ray inspector on that pixel. `--brick-size 8|16|32` regroups the voxels
into bricks of that size before the encoding.
`--camera <yaw> <pitch> <factor>` sets the camera at the start: two angles in degrees, and the
distance as a factor of the distance that shows the whole scene. For the benchmark it is the
start and the radius of the orbit.
`--list-tracers` and `--list-encodings` print what is built in. `--self-test` uses every
control of the window once and closes it (a smoke test of the window).

### Many runs at once

`scripts/run_benchmarks.py` runs the benchmark for every case of a plan file (models, encodings,
brick sizes, camera orbits; `scripts/ablation_step3.json` is the plan of step 3). It runs a tracer
again when the spread between its repeats is above 3 %, and writes `table.md` and `table.csv`
from the results files. Every number in the table comes from a results file.

```bash
python scripts/run_benchmarks.py scripts/ablation_step3.json
python scripts/run_benchmarks.py --table-only results/<folder>
```

## Work counters

Every tracer shader is compiled twice. The timed variant is the one that the benchmark times.
The counting variant (`<shader>.counting.spv`, compiled with `THESIS_COUNT_WORK`) finds the same
hits and also writes, for each ray, how many primitives it was tested against, how many steps it
took through the tracer's own search structure, how many cells of primary grids and mipmap cells
it visited (and how many mipmap squares it skipped, and at which levels), how many runs it was
tested against, and the kind of the cell with the hit. For one ray that the caller names, it also
writes every step to the ray log (`shared/include/thesis/shared/RayLog.h`, at most 4,095 steps).
In the timed variant the counter struct is empty, so counting costs it nothing
(`shaders/common/work.slang`). A test checks that the ray log of a ray agrees with its counters
and ends with its hit. After the timed repeats of each tracer the benchmark runs the
counting variant from 8 points of the orbit and adds the result to the results file as
`summary.ray_work`.

Live profiling: start the [Tracy](https://github.com/wolfpld/tracy/releases) profiler
**v0.11.1** (must match the client version in vcpkg) and connect while the renderer runs.

## Measuring rules

The benchmark follows a fixed protocol so numbers can be compared across days:

1. Warm-up runs for at least `--warmup` seconds (default 10), until the GPU clock is stable
   and the trace times of the last 3 orbits agree within 2 %, at most `--warmup-max` seconds
   (default 30). The table's "settled" column says whether it did; the system state is captured
   after warm-up. The minimum of 10 s is there because the driver runs a new shader slower for
   about its first 5 seconds (up to 1.8 times for the grid tracer), at a steady speed that a
   test for "stable" cannot tell from the real one.
2. Several repeats of one camera orbit; reported value = median of the repeat medians.
   The GPU clock is also recorded after each repeat.
3. Each results file records GPU, driver, clocks, temperature, power draw and limit, charger
   state, Windows power plan and mode, build configuration and git commit. For a voxel model
   it also records the encoding, the size of every buffer and the statistics of the encoding.
4. Validation must be off for numbers (`--validation` is for correctness only).
5. Rays and hits pass through GPU memory. That costs every tracer the same; the `null` tracer
   measures exactly that floor, and the benchmark prints each tracer's time above it.
6. Before a number is used, look at `spread_between_repeats_percent` in its results file. The
   runner script runs a tracer again when the spread is above 3 %.

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
| `src/tracers/*` | One folder per tracer plugin, with its shader and its search structure (`software_grid`, `software_octree`, `software_bvh`) | tracing |
| `src/workloads` | Ray generators (camera rays) | tracing |
| `src/resolve` | Hits → image | tracing |
| `src/metrics` | Measurement output socket: Tracy plots, JSON results, system state | core |
| `src/app` | Window, control panel (with the ray inspector), presenter, benchmark mode, frame pipeline, scene source | everything |
| `src/tools/model` | `thesis_model`: voxelize a mesh, report on the encodings | encodings, metrics |
| `shared` | Headers read by both C++ and shaders (binding slots, brick bit layout, bits of a hit, octree and BVH nodes, the ray log) | |
| `shaders/common` | Shared GPU code: formats (`types.slang`), box test (`aabb.slang`), the brick test of the 2012 format (`brick2012.slang`), the two bit layouts (`brick2012_inline.slang`, `brick2012_split.slang`), the stored mipmap (`brick2012_mipmap.slang`), the closest hit of a ray (`closest.slang`), work counters and ray log (`work.slang`), packed normals (`normals.slang`) | |
| `shaders/primitives/<kind>` | What is inside a primitive's box: `box`, `brick2012_inline`, `brick2012_split`, `brick2012_inline_mip`, `brick2012_split_mip` | |
| `scripts` | `run_benchmarks.py` and its plan files | |
| `tests` | Unit tests, encoding round trips, the cross-tracer agreement test, the brick test against a CPU test of every voxel cube and against hand-computed rays | |

Each library only sees the headers of libraries it links, so the dependency direction is
enforced by the build. Mesh, voxel and encoding code knows nothing about Vulkan.

## Extending

- **New tracer:** add `src/tracers/<name>/` with an `ITracer` implementation, its shader, and a
  `CMakeLists.txt` calling `thesis_register_plugin(SOCKET TRACER ...)`; add the folder in
  `src/tracers/CMakeLists.txt`. No other code changes; the agreement test covers it automatically.
- **New encoding:** add `src/encodings/<name>/` with an `IEncoding` implementation and a
  `CMakeLists.txt` calling `thesis_register_plugin(SOCKET ENCODING ...)`; add the folder in
  `src/encodings/CMakeLists.txt`. Add a primitive kind with the same name (next point). The
  round trip test, the cube-by-cube test, the report tool, the benchmark and the panel pick it
  up automatically.
- **New bit layout of the 2012 format:** the brick test is shared. The kind's `primitive.slang`
  only implements the four functions of `IBrickLayout` (`shaders/common/brick2012.slang`):
  read a cell, find the run words of a column, read the normal of a height cell, read the
  normal of a voxel in a column with runs.
- **New primitive kind:** add `shaders/primitives/<kind>/primitive.slang` implementing the
  contract described in `shaders/primitives/box/primitive.slang` (two functions:
  `intersectPrimitive` and `primitiveAttributes`), and add the kind to
  `THESIS_PRIMITIVE_KINDS` in `CMakeLists.txt`. Every tracer is compiled for it automatically.
  The kind's own buffers go in `core::Scene::primitiveData` (bindings 9..15); tracers pass them
  through without knowing about them.
- **Baseline tracers** (like `null`) return `false` from `ITracer::findsHits()`: they are
  benchmarked, but skipped by the correctness tests.
- **A tracer that cannot handle a scene** throws `tracing::UnsupportedScene` from `prepare()`.
  The window leaves it out and says why; the benchmark skips it unless it was asked for by name.
