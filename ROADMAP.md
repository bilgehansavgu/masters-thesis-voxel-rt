# Thesis Roadmap

This document explains what the thesis is about. It shows what exists on 12 November 2026 and which steps come next. It names each fundamental idea and does not go deeper.

## The question

In 2012, a group at TUM published a compact way to store 3D models as voxels (small cubes). The paper, "Hybrid Sample-based Surface Rendering", is in this folder. Your professor thinks that newer voxel methods are not much better. The thesis tests how well the 2012 format works on a current GPU with ray tracing hardware.

The thesis compares two tracers. A tracer is the part of the program that finds what each ray hits. The hardware tracer lets the GPU build and walk its own search structure around the data. The software tracer walks a search structure that we write, in a compute shader (a general program that runs on the GPU).

Both tracers use the same data and the same brick test (the code that finds where a ray hits inside one brick). So a difference in speed comes only from the search structure.

## The ideas you need

Ray tracing sends one ray (a start point and a direction) through each pixel. The first object that the ray hits gives the pixel its color. One image needs about two million rays. So the program needs an acceleration structure (an index that lets a ray skip empty space).

The 2012 format stores only the surface of a model. The paper calls a stored surface voxel a sample. The format has these parts:

- Brick: a small block of voxels, for example 32 × 32 × 32. The format keeps only the bricks that contain surface.
- Primary grid: one 2D grid for each brick. Each cell is one column of voxels along the axis that faces the surface best.
- Run: a group of filled voxels that touch each other in one column. The format stores the first voxel and the length.
- Height cell: a column with one filled voxel stores its height directly, like a height field (a grid of heights).
- Indirection buffer: one list that holds the runs of all other columns.
- Attributes: the normal (the direction that a surface faces) and the color of each stored voxel.
- Min/max mipmaps: small summary grids that tell a ray which regions of a brick it can skip.
- Level of detail (LoD): coarser copies of the bricks for the parts that are far from the camera.

The ray tracing hardware walks a BVH (bounding volume hierarchy: boxes inside boxes). It can test triangles without help. For other data, it gives our code each box that a ray touches, and our code finds the hit inside that box. In the thesis, one box holds one brick.

## The pipeline

```
Once for each model (offline):
  triangle mesh file -> voxelizer -> bricks -> encoder (2012 format) -> scene file

For each frame:
  camera rays -> tracer (hardware or software) -> hits -> shading -> image
```

A tracer reads a list of rays and writes a list of hits. A hit says which object the ray met and at what distance. Shading turns each hit into a color. All other parts are the same for each tracer, so each tracer gets the same rays and the same timing method. The plan covers models that fit in GPU memory.

## What exists today

Step 0 is complete. The folder `Renderer` holds a test bench that draws plain boxes in place of voxel bricks. It uses Vulkan (the graphics API) and the shader language Slang. It has these parts:

- Three tracers: `software` tests each box for each ray. `hardware` uses the BVH of the GPU through ray queries (our shader asks the BVH for each box). `null` finds nothing and shows the fixed cost.
- Two modes: a window shows the scene and lets you switch the tracer. A benchmark (a timed run with a fixed method) writes the results files.
- Automatic tests that make sure that all tracers return the same hits. A speed comparison has no value if the answers differ.

With 1920 × 1080 rays and 382 boxes, one frame takes 0.431 ms (`null`), 0.434 ms (`hardware`), and 5.560 ms (`software`). These numbers say nothing about voxels, because the box scene is too easy. `Renderer/README.md` tells you how to build and run the program. `renderer-field-guide.html` explains step 0 in detail.

## The steps

Each step ends with a result that you can see or measure, and each step needs the result of the step before it.

### Step 1: Build the voxel data

Why: the program has no voxel data. The 2012 format is the subject of the thesis, so it comes first.

What: a tool reads a triangle mesh (a surface made of triangles) and turns it into voxels. It groups the voxels into bricks and writes each brick in the 2012 format. The paper does not give the bit layout of a brick, so we must design it.

Done when: the tool can encode the Stanford models Dragon and Lucy, and a decode of the file returns the same voxels. A memory report exists for comparison with the paper (about 30 MB for Dragon).

### Step 2: Trace a ray inside a brick

Why: this step gives the first image of a real model. It is also the first comparison that means something, because each ray now does real work.

What: we write the brick test. The ray walks across the primary grid one cell at a time (a DDA, digital differential analyzer). In each cell, the code tests the ray against the runs of that column. The software tracer also gets a plain grid over the bricks, because a test of each brick for each ray is too slow.

Done when: both tracers show the same image of the model, the tests pass, and a first timing exists.

### Step 3: Make each tracer as fast as possible

Why: a comparison is fair only if each side is at its best.

What:

- Inside a brick: the ray uses the min/max mipmaps to skip empty regions.
- Software tracer: a better structure replaces the plain grid, for example an octree (a tree where each node has eight children).
- Hardware tracer: we try different box sizes, for example one box for each brick or many smaller boxes.
- Second hardware tracer: a ray tracing pipeline with intersection shaders, where the GPU calls our brick test by itself. OptiX works this way.

Done when: a table shows the effect of each change alone (an ablation).

### Step 4: Level of detail

Why: far parts of a model cover few pixels, so full detail there costs time and adds nothing to the image.

What: we merge each group of 2 × 2 × 2 bricks into one coarser brick, and each tracer selects the level that fits the distance. The software tracer can select a level for each ray. The BVH of the hardware tracer holds a fixed set of boxes, so that tracer needs a different method.

Done when: a plot shows speed and image error against the distance to the camera.

### Step 5: Other rays and other formats

Why: camera rays are orderly, because neighbor rays go almost the same way. Shadow rays and ambient occlusion rays go in scattered directions and stress a search structure in a different way. The thesis also needs one newer voxel format as a baseline (a known result to compare against).

What:

- Ray generators for shadow rays (from a surface point toward the light) and ambient occlusion rays (short rays that measure how open a point is).
- One other voxel format as the baseline. The choice depends on the papers that your professor meant.
- An OptiX tracer, if your professor wants OptiX numbers.

Done when: speed numbers exist for both ray types, and memory and speed numbers exist for the baseline format.

### Step 6: Final measurements

Why: each claim in the thesis needs a number from one fixed method on one machine.

What: we run each combination of tracer, format, model, and ray type on the RTX 4070 laptop. The results files feed the tables and figures of the thesis.

Done when: a results file exists for each claim in the thesis.

## What you must do now

1. Send the questions below to your professor.
2. Download the Stanford models Dragon and Lucy. Step 1 needs them.
3. Before step 1 starts, agree on the bit layout of a brick.
4. Put the folder `Renderer` under git (version control), so that each results file can name its code version.

## Open questions for your professor

1. Is hardware ray tracing in Vulkan acceptable in place of OptiX? OptiX can come later as one more tracer.
2. Which Eurographics papers did you mean? They decide the baseline format in step 5.
3. Is "voxels only" correct? The plan leaves out the rasterization part (the usual way a GPU draws triangles) of the 2012 paper.
4. What are the rules of the chair for AI help with the code?
