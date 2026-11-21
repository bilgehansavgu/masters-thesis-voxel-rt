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
                 └─ bitmask                baseline: one occupancy bit per voxel, list of normals

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
that the ray passes above or below. It finds the same hits as the walk without the mipmap, within
the tolerance of the agreement test (relative distance 1e-4, `thesis/core/HitComparison.h`). The software tracers
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

- *Scene*: the model, every `.voxels` file in `../Models` and the box field. A model loads in the
  background; the status bar says so. A model that cannot be built shows why, and the window
  keeps the scene before.
- *Encoding*: one switch per setting of the encodings (layout, mipmap), and the brick size.
- *Rays*: the ray order (rows or tiles) and the camera presets of the benchmark plan (standard
  x1, close x0.35, near x0.18).
- *Search*: the tracer, with the memory of its search structure.
- *Output*: the view, the light of the lit views, the colour scale of the count views.

**Image** (middle). A count view has its colour bar on the image; the mouse over it shows the
count of a colour.

**Inspector** (right). **Show the search structure** (key S) draws a horizontal cut through the
structure of the tracer at the **Cut height**: grid cells, octree cubes, BVH boxes at one **Tree
level**, or the brick boxes that the GPU gets for its own tree, with their counts. **Pick a pixel**
(or a right click without moving) records its ray on the GPU; the arrow keys step through its
stages: the ray, the search (cells, cubes or boxes visited, in gold; for hardware only the bricks
that the GPU handed over), the brick, the columns inside the brick, and the voxel it hit. **Side
view** (key V) shows the ray from the side. Another tracer records the same pixel again and shows
the work of both. **Ray log** lists every recorded step. Lines behind the model are drawn faint:
the window reads the distance to the surface of each pixel once each time the camera stops.

**Measurements** (under the image):

- *Timing*: quick live timing of every tracer at this camera (key M) and the stages of the frame
  on screen. Window numbers, not thesis numbers.
- *Work per ray*: the average work of a ray in the frame, with a work view on.
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
**B** the brick size (with Shift: the one before), **1-9, 0, -, =** the view. **M** measures
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
| 0 | Steps through the tracer's own search structure (the hardware tracer cannot count them) |
| - | Mipmap cells read |
| = | The largest mipmap square that the walk skipped: blue 2x2 cells, green 4x4, yellow 8x8, orange 16x16, red 32x32 |

Work views and the frame that captures a selected pixel run the counting shader. Exploring the
saved ray uses its recorded events. The comparison table only accepts ordinary trace frames. With a
work view on, the panel also shows the average work of a ray in the frame, about once a second.

The **ray inspector** lists every step of one ray in order: the cells or nodes of the search
structure, the primitives it was tested against, and inside each brick the mipmap squares (with
their lowest and highest voxel, and whether the ray skipped them), the cells and the runs, then
the hit. "Copy as text" puts the list on the clipboard. The hardware tracer cannot report the
nodes of its BVH, so its list starts at the primitives.

`--benchmark` is headless and writes `results/<time>_<tracer>[_<encoding>].json`.
`--screenshot <file.png>` is headless too: it writes one frame, with `--view <1-12>`. A count
view (6, 7, 8, 10, 11) gets its colour bar in a strip below the image: viridis, black for 0,
white above `--scale-max <count>` (default 64), linear or with `--log-scale` log2. With
`--inspect <x> <y>` it also prints every step of the ray of that pixel. In the window,
`--inspect` starts the ray inspector on that pixel. `--brick-size 8|16|32` regroups the voxels
into bricks of that size before the encoding.
`--camera <yaw> <pitch> <factor>` sets the camera at the start: two angles in degrees, and the
distance as a factor of the distance that shows the whole scene. For the benchmark it is the
start and the radius of the orbit. `--exact-camera <x> <y> <z> <yaw> <pitch> <distance>` sets the
camera as the window has it (target, angles, distance), as "Copy as command" writes it.
`--light <azimuth> <elevation>` (degrees) moves the light of views 1 and 5, `--distance-range
<near> <far>` sets the range of view 2. `--view`, `--scale-max`, `--log-scale` and `--light` set
the start of the window too.
`--ray-order rows|tiles` sets where the ray of each pixel sits in the ray list. The GPU traces
32 neighbouring rays of the list together. With `rows` (the default) these are 32 pixels of one
row; with `tiles` they are a tile of 8 x 4 pixels. The image is the same either way; only the
speed can change. The rule is in `shared/include/thesis/shared/RayOrder.h`.
`--list-tracers` and `--list-encodings` print what is built in (an encoding with its settings).
`--self-test` uses every control of the window once, then checks "Copy as command": it renders
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
