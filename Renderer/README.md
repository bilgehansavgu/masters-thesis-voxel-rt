# Thesis Renderer

Testbed for ray tracing compressed voxel data on GPUs with ray tracing hardware: the GPU's own
search structure (hardware tracers: ray queries, and a ray tracing pipeline with and without
shader execution reordering) against our own (software tracers: grid, octree, BVH), behind one
interface.

## The idea in one picture

```
OFFLINE, once per model                                   (thesis_model voxelize)
  mesh file (PLY, OBJ, glTF) → voxelizer → voxel file (.voxels)

WHEN A MODEL IS LOADED, and again when the encoding is switched
  voxel file → (bricks of 8, 16 or 32 voxels) → [ ENCODING SOCKET ] → buffers on the GPU, one primitive per brick
                 ├─ brick2012_inline       2012 format, attributes next to the heights (the paper)
                 ├─ brick2012_split        2012 format, attributes in their own buffer
                 ├─ brick2012_inline_mip   the same with the min/max mipmap of each brick
                 ├─ brick2012_split_mip    the same with the min/max mipmap of each brick
                 └─ bitmask                baseline: one occupancy bit per voxel, list of normals

EVERY FRAME
  camera rays → [ TRACER SOCKET ] → hits → shading → screen
                  ├─ hardware                       Vulkan ray queries walk a GPU-built BVH
                  ├─ hardware_pipeline              the same BVH, ray tracing pipeline: brick test in an intersection shader
                  ├─ hardware_pipeline_reorder_hit  + shader execution reordering after the hit (a control)
                  ├─ hardware_pipeline_reorder_box  BVH finds the first box, rays regrouped by box, then the brick test
                  ├─ software_grid                  compute shader, walks our own uniform grid
                  ├─ software_octree                compute shader, walks our own sparse octree
                  ├─ software_bvh                   compute shader, walks our own binary BVH (SAH build on the CPU)
                  ├─ null                           baseline: moves rays and hits, searches nothing
                  └─ (later)                        OptiX, only if the professor wants it

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
that the ray passes above or below. It finds the same hits as the walk without the mipmap, within
the tolerance of the agreement test (relative distance 1e-4, `thesis/core/HitComparison.h`). The software tracers
walk a uniform grid, a sparse octree or a binary BVH of their own. The window shows what each
ray does: work views, live averages, and the ray inspector, which lists every step of one ray.

The hardware tracers all walk the same GPU-built BVH (`tracing::BoxBvh`). `hardware` asks it
from a compute shader (ray queries). The `hardware_pipeline` tracers run a ray tracing pipeline
(`vkCmdTraceRays`, the model OptiX uses), and two of them regroup the rays with shader execution
reordering (SER, `VK_NV_ray_tracing_invocation_reorder`). `hardware_pipeline_reorder_box` is
exact only when no two primitive boxes overlap (bricks and the box field do not); for other
scenes it says so and is skipped. On a GPU without ray tracing pipelines or without active
reordering, the tracers that need them are skipped with the reason.

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
  next to each `.spv`. Creating a kernel or ray tracing pipeline fails with a clear message if a shader reads a binding
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
build/src/app/Release/thesis_renderer.exe                                       # Explore Lucy 2048 if available
build/src/app/Release/thesis_renderer.exe --boxes                               # box tracing example
build/src/app/Release/thesis_renderer.exe --model ../Models/lucy-2048.voxels    # a voxel model
build/src/app/Release/thesis_renderer.exe --benchmark --model ../Models/lucy-2048.voxels --encoding brick2012_split
build/src/app/Release/thesis_renderer.exe --model ../Models/lucy-2048.voxels --screenshot lucy.png --camera 90 10 0.75
build/src/app/Release/thesis_renderer.exe --model ../Models/lucy-2048.voxels --encoding brick2012_inline_mip --tracer software_octree --screenshot ray.png --inspect 960 540
build/src/app/Release/thesis_renderer.exe --help
```

To repeat the empty-space skipping comparison with the benchmark runner, use the following from `Renderer`.

```bash
build/src/app/Release/thesis_renderer.exe --model ../Models/lucy-2048.voxels --compare-skipping --tracer hardware
build/src/app/Release/thesis_renderer.exe --open-result path/to/fixed-view-result.json
```

The window opens on Lucy (`Models/lucy-2048.voxels`) if that file exists, otherwise on a field of
solid boxes. Docked areas sit around the image (drag a tab to rearrange them):

**Settings** (left), in the order a ray meets them:

- *Scene*: the model, every `.voxels` file in `../Models`, files opened with *Open...* (any
  folder), recently opened files, and the box field. A model loads in the background; the status
  bar says so. A model that cannot be built shows why, and the window keeps the scene before.
  *Import mesh* does `thesis_model voxelize` in the window: a mesh file (`.ply`, `.obj`, `.gltf`,
  `.glb`; typed, picked with *Browse...* or from the recent meshes), the resolution (a list, or
  any multiple of 32), the up axis and the rule. Before the import it estimates the voxels from an
  earlier import of the same mesh. It writes `<mesh>-<resolution>.voxels` into `../Models` (over
  a file of that name, marked before) and opens it; *Cancel* stops it and writes nothing. Recent
  files are kept in `%LOCALAPPDATA%/thesis_renderer/recent.json`. A right click on a model of the
  list: *Show in Explorer*, *Remove from list* (the file stays; a file of `../Models` is back at the
  next start) or *Move to Recycle Bin...* (asks first). The model on screen cannot be removed.
- *Encoding*: one switch per setting of the encodings (layout, mipmap), and the brick size.
- *Rays*: the ray order (rows or tiles) and the kept cameras. They start as the cameras of the
  benchmark plan (standard x1, close x0.35, near x0.18). **Keep view** adds the current camera under
  a name; a right click renames a camera, moves it to the current view or deletes it; **Restore
  defaults** brings back the three. A camera is kept relative to the scene (angles, distance
  factor, target offset in scene radii), so it works on every model. Kept in
  `%LOCALAPPDATA%/thesis_renderer/cameras.json`.
- *Search*: the tracer, with the memory of its search structure. **A/B image** picks a second
  tracer: the image then shows the selected tracer (A) left of a line and the second one (B) right
  of it, from the same camera rays, in the same view and colour scale. Drag the line. Both trace in
  every frame, so the window keeps no trace times meanwhile.
- *Output*: the view, the light of the lit views, the colour scale of the count views. **Range,
  automatic** (on at the start): the distance view spans the nearest to the farthest visible hit,
  and a count view ends its colour map at the 99th percentile of the rays with work. A change by
  hand switches it off, so the colours of two tracers compare. **Per warp, largest in the warp**:
  each pixel of a count view shows the largest count of its warp (the 32 neighbouring rays of the
  ray list that the GPU runs together); the warp is busy until its slowest ray is done. Not for the
  ray tracing pipeline tracers: the driver decides which rays share a warp.

The bar in the top left corner of the image opens or folds the three windows (a window's close
button folds it too); **Image only (F)** folds them all, F again opens them. **Save (P)** writes
the frame as a PNG with its legend (as `--screenshot` does) and a `.txt` with the command that
renders it again, into the folder of the last image (first `Pictures/thesis_renderer`); with A/B on
it also writes the B image. **Save as...** asks for the file.

**Image** (middle). Every view whose colours stand for values has its legend on the image: a
colour bar with ticks for the count views and for the distance, named colour boxes for the two
category views (9, =). The mouse over a bar shows the value of a colour. Views 1, 3, 4 and 5
have none.

**Inspector** (right). **Show the search structure** (key S) draws a horizontal cut through the
structure of the tracer at the **Cut height**: grid cells, octree cubes, BVH boxes at one **Tree
level**, or the brick boxes that the GPU gets for its own tree, with their counts. **Pick a pixel**
(or a right click without moving) records its ray on the GPU; the arrow keys step through its
stages: the ray, the search (cells, cubes or boxes visited, in gold; for the hardware tracers only the bricks
that the GPU handed over), the brick, the columns inside the brick, and the voxel it hit. **Side
view** (key V) shows the ray from the side. Another tracer records the same pixel again and shows
the work of both. **Ray log** lists every recorded step. Lines behind the model are drawn faint:
the window reads the distance to the surface of each pixel once each time the camera stops.

**Measurements** (under the image):

- *Timing*: quick live timing of every tracer at this camera (key M) and the stages of the frame
  on screen. Window numbers, not thesis numbers.
- *Work per ray*: the work of the rays in the frame, with a work view on: the mean over all rays
  and over the rays with a hit, the 99th percentile, the most, and the warp use (the work of all
  rays divided by the work their warps were busy for; 100 % means no ray waited for another). The
  arrow of a row opens the inspector on the ray with the most. A click on a row shows its
  histogram: rays per count, one bar per power of two, rays with and without a hit stacked, the
  height on a log scale.
- *Compare view*: the benchmark protocol twice at the current camera (mipmap off/on, or a second
  tracer) on one frozen buffer of camera rays. Both runs get the same rays, and the hits are
  compared with the rule of the agreement test. Both results files are saved together. **Show**
  restores the camera, tracer and encoding of a row. The command line benchmark accepts
  `--fixed-view` for a stationary camera; its default camera path is a full orbit.
- *Memory*: every buffer of the scene, and the size of each encoding at this brick size.
- *Command*: the `thesis_renderer` command that makes the frame on screen (screenshot) or measures
  its scene (benchmark: an orbit at a camera preset, else a fixed view at this camera), with
  buttons that copy them.

The **status bar** at the bottom always shows the model, the encoding settings, the brick size,
the ray order, the camera, the tracer, the view and the trace time.

In the window: left-drag to orbit, scroll to zoom. Hold the right button to fly: the mouse turns
the view, **W/A/S/D** move, **Q/E** go down and up, **Shift** is faster, scrolling sets the speed. **Tab** switches the tracer, **E** the encoding,
**B** the brick size (with Shift: the one before), **1-9, 0, -, =, C** the view. **F** folds every
window (image only), **P** saves the image. **M** measures
every tracer at the current camera ("Quick timing"), **R** resets the camera. **S** shows the
search structure, the **arrow keys** step along a picked ray, **V** shows it from the side. A **right click**
(without moving the mouse) on the image starts the ray inspector on that pixel, **Esc** stops it. The panel shows the size
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
| 0 | Steps through the tracer's own search structure (the hardware tracers cannot count them) |
| - | Mipmap cells read |
| = | The largest mipmap square that the walk skipped: blue 2x2 cells, green 4x4, yellow 8x8, orange 16x16, red 32x32 |
| C | GPU clock cycles of the ray, read with the clock of its warp: where the time goes, for every compute shader tracer that counts work (the ray query hardware tracer too), not for ray tracing pipeline tracers (after TraceRay a ray may run on another SM, whose clock differs). From the counting shader: for looking, never a thesis number |

Work views and the frame that captures a selected pixel run the counting shader. Exploring the
saved ray uses its recorded events. The comparison table only accepts ordinary trace frames. With a
work view on, the panel also shows the average work of a ray in the frame, about once a second.

The **ray inspector** lists every step of one ray in order: the cells or nodes of the search
structure, the primitives it was tested against, and inside each brick the mipmap squares (with
their lowest and highest voxel, and whether the ray skipped them), the cells and the runs, then
the hit. "Copy as text" puts the list on the clipboard. The hardware tracers cannot report the
nodes of its BVH, so its list starts at the primitives.

`--benchmark` is headless and writes `results/<time>_<tracer>[_<encoding>].json`.
`--screenshot <file.png>` is headless too: it writes one frame, with `--view <1-13>`. A view
with a legend (2, 6 to 13) gets it in a strip below the image, as in the window. Count views
(6, 7, 8, 10, 11, 13): viridis, black for 0, white above `--scale-max <count>` (default 64, for view 13 20000), linear
or with `--log-scale` log2; `--warp-max` shows the largest count of each warp. With
`--inspect <x> <y>` it also prints every step of the ray of that pixel. In the window,
`--inspect` starts the ray inspector on that pixel. `--brick-size 8|16|32` regroups the voxels
into bricks of that size before the encoding.
`--camera <yaw> <pitch> <factor>` sets the camera at the start: two angles in degrees, and the
distance as a factor of the distance that shows the whole scene. For the benchmark it is the
start and the radius of the orbit. `--exact-camera <x> <y> <z> <yaw> <pitch> <distance>` sets the
camera as the window has it (target, angles, distance), as "Copy as command" writes it.
`--light <azimuth> <elevation>` (degrees) moves the light of views 1 and 5, `--distance-range
<near> <far>` sets the range of view 2. `--view`, `--scale-max`, `--log-scale`, `--light` and
`--distance-range` set the start of the window too; a range given this way turns the window's
automatic ranges off, so the window shows what the command says.
`--ray-order rows|tiles` sets where the ray of each pixel sits in the ray list. The GPU traces
32 neighbouring rays of the list together. With `rows` (the default) these are 32 pixels of one
row; with `tiles` they are a tile of 8 x 4 pixels. The image is the same either way; only the
speed can change. The rule is in `shared/include/thesis/shared/RayOrder.h`.
`--list-tracers` and `--list-encodings` print what is built in (an encoding with its settings).
`--self-test` uses every control of the window once (it imports a small sphere into
`%TEMP%/thesis_self_test`, not into `../Models`), then checks "Copy as command": it renders
the frame on screen again from its screenshot command and exits with code 3 if one pixel differs.

### Many runs at once

`scripts/run_benchmarks.py` runs the benchmark for every case of a plan file (models, encodings,
brick sizes, camera orbits, and optionally `ray_orders`; `scripts/ablation_step3.json` is the plan
of step 3, `scripts/ray_order.json` compares the two ray orders). It runs a tracer
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
tested against, the kind of the cell with the hit, and the GPU clock cycles from its start to its
end (`VK_KHR_shader_clock`, clock of the warp: a ray that finishes early waits for the slowest ray
of its warp). The cycles time the counting shader, so they show where the time goes, never a
result. A tracer whose rays may move to another SM during the trace (every ray tracing pipeline:
Vulkan may repack the invocations at TraceRay, with or without shader execution reordering)
reports no cycles (`ITracer::countsCycles`): the SM clocks differ, so the difference is not valid.
The results file then has `null` for the cycles, as for any counter a tracer does not report. For one ray that the caller names, it also
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
5. Rays and hits pass through GPU memory, the same for every tracer. The `null` tracer only
   reads the rays and writes misses, so its time shows how much of a trace is this memory
   traffic. The trace time itself is the number to report. The time above the null tracer is
   only a hint: the GPU reads memory and searches at the same time, so the difference is not
   the cost of the search. Never divide two of these differences.
6. Before a number is used, look at `spread_between_repeats_percent` in its results file. The
   runner script runs a tracer again when the spread is above 3 %.

For thesis numbers: plugged in, same Windows power mode, nothing else running.

## Layout and extending

[`ARCHITECTURE.md`](ARCHITECTURE.md) owns the structure of the code: the folders and what each
one may use, the sockets and their rules, and how to add a tracer, an encoding, a bit layout or
a primitive kind.
