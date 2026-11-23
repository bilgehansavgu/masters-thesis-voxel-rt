# ARCHITECTURE

Renderer is test bench of master's thesis. Question: how well 2012 voxel brick format (Reichl et al., "Hybrid Sample-based Surface Rendering") trace on GPU with ray tracing hardware? GPU own BVH (hardware tracer) against search structure we write (software tracer). Brick stay same. Only search around brick change.

## 1. Rules You Cannot Break

### 1.1 Hold Constant, Change One
Thesis compare search structure. Everything else same for every tracer: same ray buffer, same brick test, same shading, same timer.
- Tracer own how ray find primitive. Nothing else.
- Tracer reach primitive only through `import primitive;`. Brick test of 2012 format live once, `shaders/common/brick2012.slang`, behind that import.
- Test: change give one tracer other ray, other brick code or other shading? Comparison broke. Example: hardware tracer and octree tracer both call same `intersectPrimitive`.

### 1.2 Data -> Work -> View
Arrow point one way. Never back.

| Layer | Is | Rule |
|---|---|---|
| **Data** | mesh, voxel model, encoded scene, `shared/` header, results file | Know nothing. No Vulkan. Same input give same bytes. |
| **Work** | encoding, tracer, workload, resolve, frame pipeline, benchmark | Make truth: buffer, hit, number. Read data format, never redefine it. |
| **View** | window, control panel, overlay, structure view, ray inspector | Show truth. Never own, decide or copy it. May keep display state: picked pixel, cut height, cached text. |

**Talk across**
- Caller know plugin by name only. `core::Registry` make it.
- Tracer never name buffer of primitive kind. Ask `GpuScene` (`primitiveBindings`, `bindPrimitiveData`).
- Measuring code never know where number go. Hand it to `metrics::MetricSink`.
- View read what tracer record (ray log, work counter) and what tracer hand over. Tracer describe own structure as plain shape (`ITracer::diagram`, `IStructureDiagram`), view only draw it. Test: view include header of tracer folder? Wrong. Example: `StructureView` draw cut of every tracer, know none.

### 1.3 Socket And Plugin
Socket = extension point with one rule. Plugin = folder that fill socket.

| Socket | One rule | Where | Proof |
|---|---|---|---|
| Tracer | ray list in, hit list out, same index | `tracing::ITracer` | agreement, known-answer, cube-by-cube test |
| Encoding | `decode(encode(model))` give back same voxels | `encoding::IEncoding` | round trip test, `thesis_model report` |
| Primitive kind | two function: `intersectPrimitive`, `primitiveAttributes` | `shaders/primitives/<kind>/primitive.slang` | every tracer compile for it |
| Brick layout | four function of `IBrickLayout` | `shaders/common/brick2012.slang` | cube-by-cube test |
| Measurement output | number go out, measurer not know where | `metrics::MetricSink` | results file, Tracy plot |

- Plugin register by `thesis_register_plugin(...)` in own `CMakeLists.txt`. CMake write call list.
- Primitive kind chosen at compile time. Tracer compile once per kind, plus counting variant. No runtime switch in shader. No copy of tracer per kind.
- Encoding name = primitive kind name.

### 1.4 One Job Each
One library, one job. One class, one job.
- `tracing` own socket and ray batch. Not ray making (`workloads`), not image (`resolve`), not number output (`metrics`).
- `gpu` thin Vulkan layer. Know no ray, no voxel.
- `FramePipeline` own frame order: rays -> trace -> shade, each stage timed. Window, benchmark, screenshot run its stage code. Run mode may leave stage out (benchmark skip shade). Test: run mode make ray, trace or shade with own code? Wrong. Example: benchmark call `FramePipeline::trace`, same as window.
- `app` glue. Own policy of window and run mode. Never own reusable truth. Test: test or tool need it too? Move it down. Example: hit comparison live in `core`, test and benchmark both use.

### 1.5 Open Closed
New plugin cost zero old code. Allowed old touch: one `add_subdirectory` line, and `THESIS_PRIMITIVE_KINDS` list for new primitive kind.
No `if (name == "hardware")`. No `switch` on tracer or encoding. Ask interface instead. Full list in `Tracer.h` and `Encoding.h`. Main ones:
- `findsHits()` false = baseline. Correctness test skip it. Example: `null`.
- `countsWork()` false = no counting variant.
- `prepare()` throw `UnsupportedScene` = tracer cannot do scene. Window hide it and say why. Benchmark skip it unless asked by name.
- `countsStructureSteps()` false = no step count through search structure. Example: `hardware`, GPU hide its BVH walk.
- `text()` = label and sentence people read. `diagram()` = structure the window draw. `describeStructureStep()` = one step through structure in ray log, as words. Ray log has one kind, `THESIS_LOG_STRUCTURE_STEP`, for every tracer; tracer give its fields meaning.
- Encoding: `skipsEmptySpace()`, `skippingTwin()` = skipping off / on pair. Never read `_mip` from name.
- Start choice: plugin register with `core::Pick::Default`. Caller ask `registry.defaultName()`.

Test: tracer name appear outside own folder? Wrong. Text about one tracer live in tracer. `architecture_check` test catch name in string. Test that run every plugin never name one. Test of one plugin (include that plugin header) may.

### 1.6 Measure Honest
Number in thesis must survive "how you know?".
- Timed variant make number. Counting variant (`THESIS_COUNT_WORK`) is diagnosis, never timed. Test: results number come from counting shader? Wrong. Example: timed variant counter struct empty, cost zero.
- `gpu::GpuTimer` is truth for results. Tracy is live look only.
- Ray and hit pass through GPU memory, same for every tracer. `null` tracer show how much of trace time is that traffic. Thesis number = raw trace time. Time above `null` is hint only: GPU read memory and search at same time, so difference is not search cost. Test: thesis divide two "above null" number? Wrong. Example: table show `null` time beside each tracer time, never tracer minus `null` as search cost.
- Thesis number come from results file. Results file name git commit, GPU, clock, power state. Table made by `scripts/run_benchmarks.py`, never by hand.
- Validation layer off for number, on for test.
- Benchmark protocol (warm-up, repeat, median, spread) live in [README.md](README.md#measuring-rules). Never repeat here.

### 1.7 Write Once
Thing C++ and shader both know is written one place.
- Number many library know: `shared/include/thesis/shared/`. Socket binding slot: `Bindings.h`. Bits and binding of 2012 brick: `Brick2012.h` (`shaders/common` and window read it). Hit bits, ray log: same folder.
- Number one plugin know (own binding, node bits): `<Name>Shared.h` in plugin own `include/`. Shader build put that folder on search path. Primitive kind shader see `include/` of every encoding. Example: `software_octree/.../OctreeShared.h`, `bitmask/.../BitmaskShared.h`.
- Only `#define` and comment in these header, both language read it.
- GPU struct: C++ mirror with `static_assert`, in `core/GpuTypes.h` (or plugin header for plugin struct). `TestShaderLayouts` compare to shader field by field. Kernel creation fail loud when shader read binding C++ not give.
- Test: same number in `.cpp` and `.slang`? Move it to shared header. Header in `shared/` read by one plugin only? Move it into that plugin.

### 1.8 C++ Way
- C++20. Namespace `thesis::<library>`. Interface name start `I`. File name = main class name without `I` (`ITracer` in `Tracer.h`). Small struct may live beside its class.
- Warnings are error. Code follow `.clang-format`. `format_check` test red on unformatted code.
- Error throw. Message say what and where. Vulkan call wrap in `THESIS_VK_CHECK`. Bad input = `std::runtime_error` or `std::invalid_argument`. Wrong use by programmer = `std::logic_error`. No silent fallback: silent return only when absence legal. Example: tracer cannot do scene = throw `UnsupportedScene`.
- Fix cause, not symptom. No workaround, no technical debt. Limit you cannot fix = `WHY` comment on code + tell human.
- Well-known library beat own code. Dependency come through vcpkg. `vcpkg.json` is list. New dependency = ask human first.
- Code comment stay normal prose. Say what and why.
- Build, run, measure obey [README.md](README.md). Knowledge, why of decision, and result live in [wiki](https://github.com/bilgehansavgu/masters-thesis-voxel-rt/wiki). Never repeat here.

## 2. Every Header Say Side
Code run in three place: CPU only, GPU through Vulkan, or read by both C++ and Slang. Window code get fourth tag, because it must stay away from GPU work. Wrong side = Vulkan leak into data code, test need GPU for no reason. Line under `#pragma once` carry one word, `// Side: <word>` (`/* Side: Shared */` on first line for shared header):
- `CPU` - no Vulkan. Make or check data. Test run without GPU.
- `GPU` - own Vulkan object or record command.
- `Shared` - C++ and Slang both include. Only `#define` and comment.
- `View` - window code. Show truth, own none. Include no `GPU` header: read GPU result as plain data. Example: `ControlPanel.h` include `ShadingParams.h`, not `HitShading.h`.

Folder fix side of library: `core`, `mesh`, `voxel`, `encoding`, `encodings`, `metrics`, `tools/model` are `CPU`. They link no Vulkan, so build stop Vulkan header there. Header inside `GPU` library that run on CPU still say `CPU`. Example: `software_octree/Octree.h` build octree on CPU. Header that mix side = split it.

## 3. Rules For Editing This Doc
1. Body stay caveman english. Short word. No article. Code comment stay normal prose.
2. Edit only when must. Doc is law, not notebook. Code change is not reason to edit.
3. Never invent new section. Ask human first.
4. Line earn place only when it change how next task start. Fact about one class or one shader go as `WHY` comment on that thing. Doc may point, never retell.
5. Rule take delete-test shape: name, one-line test, one-line example. Rule stand on test and example alone. Long because (bug story, driver quirk, version number) go in `WHY` comment on code or wiki. Doc may keep one-clause because.

## 4. Folder Structure
Folder show owner. One library = one folder = one CMake target `thesis::<name>`, made by `thesis_add_library`. Library keep its code, header and shader together.

Dependency point one way. No cycle. Build enforce it: library see only header of library it link. Which is lower? Delete test: A may use B only when B still whole after A gone. `workloads` use `tracing` - delete `workloads`, `tracing` still whole. So `tracing` never know `workloads`.
Plugin never link other plugin. Code two plugin share go down into socket library. Test: delete one plugin folder, other plugin still build? Must. Example: `GridFrame` live in `tracing`, grid and octree tracer both stand on it.

```
app ──► every library below. Glue, nobody use it.

tracers/*, workloads, resolve ──► tracing ──► gpu ──► profiling
                                     └──► core
metrics ──► core, profiling
encodings/* ──► encoding ──► voxel ──► mesh
                   └──► core
tools/model ──► encodings/*, metrics, voxel, mesh
shared/ (header only) ◄── every library that share number with shader
```

```
Renderer/
  src/<library>/
    include/thesis/<library>/   public header. Only what other library need
    *.cpp, *.h                  private code
    shaders/                    shader library own
  src/<socket>s/<name>/         one plugin. Own its code, shader, search structure

<library> is one of:
  core/         CPU. Shared word: scene, camera, GPU struct mirror, registry, statistics, hit comparison. Know nobody (glm only)
  mesh/         CPU. Triangle mesh, mesh file reader
  voxel/        CPU. Voxel model, voxelizer, voxel file, packed normal, regroup into brick
  encoding/     CPU. Encoding socket: IEncoding, memory report
  encodings/    CPU. Encoding plugin, one folder per format family
  profiling/    CPU marker on Tracy timeline
  gpu/          GPU. Thin Vulkan: device, buffer, image, compute kernel, ray tracing pipeline, timer, acceleration structure
  tracing/      GPU. Tracer socket: ITracer, ray batch, GPU scene, trace kernel, trace pipeline, box BVH, ray log, grid frame
  tracers/      GPU. Tracer plugin, one folder each
  workloads/    GPU. Ray maker: camera rays. Know no tracer
  resolve/      GPU. Hit list -> image. Know no tracer
  metrics/      CPU. Measurement output socket: Tracy plot, JSON results file, system state
  app/          Glue: window, panel, ray inspector, benchmark, screenshot, frame pipeline. Use all, nobody use it
  tools/model/  CPU. thesis_model: voxelize mesh, report on encodings

shared/include/thesis/shared/  Shared. Header C++ and Slang both read, known by more than one library
shaders/common/                GPU code every tracer share: formats, box test, brick test, layouts, mipmap, closest hit, work counter, normals
shaders/primitives/<kind>/     what is inside one primitive box. Kind name = encoding name
tests/                         Catch2. One file per area: Test<Area>.cpp. Helper beside it
scripts/                       run_benchmarks.py and plan file, check_architecture.py
cmake/                         build law: library default, plugin registration, shader compile, format
results/                       results file. Ignored by git
../Models/                     mesh and voxel file. Ignored by git, too big
```

In `src/`, never make `utils`, `helpers` or `managers` (test helper in `tests/` is fine). One exception: `shaders/common/` is home for GPU code every tracer share. Test: code there used by one tracer only? Move to that tracer's `shaders/`.
Find thing: know owner -> open owner folder. Know class -> file name match class.

### 4.1 Extend
- **New tracer**: `src/tracers/<name>/` with `ITracer` (`text()`; `diagram()` and `describeStructureStep()` if it has structure), shader, `<Name>Shared.h` for own binding (slot 2..6) and bits, `CMakeLists.txt` calling `thesis_register_plugin(SOCKET TRACER ...)`. Add folder in `src/tracers/CMakeLists.txt`. Agreement test cover it without change. Compute shader tracer stand on `TraceKernels`. Ray tracing pipeline tracer stand on `TracePipelines`, shader built with `thesis_add_shaders(... RAY_TRACING)`. Tracer that let GPU search use `BoxBvh`, so all walk same tree. One plugin may register several tracer, one per question. Example: `hardware_pipeline` register three.
- **New encoding**: `src/encodings/<name>/` with `IEncoding`, `<Name>Shared.h` for own bits and binding (slot 9..15), `CMakeLists.txt` calling `thesis_register_plugin(SOCKET ENCODING ...)`. Add folder in `src/encodings/CMakeLists.txt`. Add primitive kind of same name. Round trip test, cube-by-cube test, report tool, benchmark and panel pick it up without change.
- **New bit layout of 2012 format**: brick test shared. Kind's `primitive.slang` give only four function of `IBrickLayout`.
- **New primitive kind**: `shaders/primitives/<kind>/primitive.slang` keep contract in `shaders/primitives/box/primitive.slang`. Add kind to `THESIS_PRIMITIVE_KINDS` in `CMakeLists.txt`. Kind buffer go in `core::Scene::primitiveData` (binding 9..15). Tracer pass it through, never read it.

## 5. Decision Already Made
Do not argue these again without human. Why of each live in wiki decision table.
- Voxels only. Input is triangle mesh, voxelized offline. Data in-core.
- Vulkan first. Hardware tracer = ray queries. Hardware pipeline tracer = ray tracing pipeline, same model as OptiX, with and without shader execution reordering (SER). Software tracer = compute shader. OptiX later only as plugin, only if professor want.
- SER through `VK_NV_ray_tracing_invocation_reorder`, not EXT. Switch to EXT = vcpkg baseline bump = ask human.
- Slang for every shader. SPIR-V compiled at build time.
- Bit layout of 2012 format is our design. Paper not specify it. Never credit paper for it.
- Plugin registered by CMake, not by static object, not by hand list.
- Primitive kind at compile time.
- Tracers agree within tolerance, not bit-exact. Tolerance live in `core/HitComparison.h`.
- Thesis number from RTX 4070 Laptop GPU (8 GB), plugged in.
- Catch2 for test. Tracy for live profile. No own framework on top.
- Code in this repo. Knowledge, decision, result in wiki.

## 6. Proof By Test
Proof is automated test. `ctest --preset release`. Green = proven.
`format_check` hold format. `architecture_check` (`scripts/check_architecture.py`) hold plugin name in own folder (and in test of that plugin), side tag on every header, no Vulkan and no `GPU` header in `CPU`, `Shared` or `View` file. `.cpp` take side of its header.

Three proof per socket:
- Tracer: all tracers agree on same rays. Tracers match hand-computed hit. Tracers match CPU test of every voxel cube (double precision, own code, share nothing with shader).
- Encoding: round trip give same voxels.
- Shared struct: layout same in C++ and every shader.

Rule:
- New plugin picked up by existing test alone. Never copy test per tracer.
- Mechanic and its test ship in same change. No test = not done.
- GPU test tagged `[gpu]`. Validation layer on. Validation message = red.
- Change test only when assert was wrong. Test that caught real thing is never wrong. Setup may change, assert is claim.
- No loosening tolerance until red test go green. Fix code, or bound limit with number and tell human.
- Known limit live as number in test, never as comment alone.
- Counting variant must find same hits as timed variant. Ray log must agree with work counter and end at its hit.
