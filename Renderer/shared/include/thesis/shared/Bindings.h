/* ============================================================================
 * READING ORDER: FILE 12 (Part 2: GPU Rendering & Tracing Pipeline)
 * ============================================================================
 * PURPOSE:
 * Defines the Vulkan Descriptor Binding slots ( numbered connection ports between
 * C++ GPU buffers and Slang GPU shaders).
 *
 * Included verbatim by C++ and by Slang shaders. Both languages run the C
 * preprocessor, so this file may contain nothing but `#define` and `/* ... */`
 * comments.
 *
 * ----------------------------------------------------------------------------
 * HOW BINDING SLOTS ARE PARTITIONED (Descriptor Set 0)
 * ----------------------------------------------------------------------------
 *   Slots 0..1:   Input Ray buffer (0) and Output Hit buffer (1) (`RayBatch`).
 *   Slots 2..7:   Reserved for the Tracer's own search structure (e.g., Slot 2
 *                 is the Vulkan Hardware Ray Tracing TLAS).
 *   Slot  8:      Every primitive's world-space AABB (`StructuredBuffer<Aabb>`).
 *   Slots 9..15:  The primitive encoding's own GPU buffers (`Scene::primitiveData`):
 *                   - 9:  `BrickModelHeader` (grid origin, voxelSize, brickSize, etc.)
 *                   - 10: `BrickRecord` table (one 16B record per brick)
 *                   - 11: `cells` (primary grid cells of all bricks)
 *                   - 12: `indirection` (16-bit run words & offsets)
 *                   - 13: `attributes` (split layout only: 16-bit octahedral normals)
 * ============================================================================
 */
#ifndef THESIS_SHARED_BINDINGS_H
#define THESIS_SHARED_BINDINGS_H

/* Tracing kernels: workloads, tracers and primitive kinds (descriptor set 0). */
#define THESIS_BINDING_RAYS 0 /* ray list, read */
#define THESIS_BINDING_HITS 1 /* hit list, write */

#define THESIS_BINDING_TRACER_FIRST 2 /* 2..7 belong to the tracer itself */
#define THESIS_BINDING_TRACER_LAST 7
#define THESIS_BINDING_HARDWARE_SCENE 2 /* hardware tracer: the GPU-built search structure */

#define THESIS_BINDING_PRIMITIVE_BOUNDS 8      /* every primitive kind gets its bounds here */
#define THESIS_BINDING_PRIMITIVE_DATA_FIRST 9  /* 9..15: the primitive kind's own data */
#define THESIS_BINDING_PRIMITIVE_DATA_LAST 15

/* Primitive kinds brick2012_inline and brick2012_split (the layouts are in Brick2012.h). */
#define THESIS_BINDING_BRICK_HEADER 9       /* one BrickModelHeader */
#define THESIS_BINDING_BRICK_TABLE 10       /* one BrickRecord per brick */
#define THESIS_BINDING_BRICK_CELLS 11       /* the primary grid cells of all bricks */
#define THESIS_BINDING_BRICK_INDIRECTION 12 /* 16-bit words: the runs of columns with more than one voxel */
#define THESIS_BINDING_BRICK_ATTRIBUTES 13  /* split layout only: one 16-bit normal per voxel */

/* Shading kernel (descriptor set 0). */
#define THESIS_BINDING_SHADE_HITS 0
#define THESIS_BINDING_SHADE_OUTPUT 1

#endif
