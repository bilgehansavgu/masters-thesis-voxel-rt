/* Descriptor binding slots: the ONLY place a slot number is written.
 *
 * Included verbatim by C++ (src/tracing/include/thesis/tracing/Bindings.h and others) and by
 * Slang shaders. Both languages run the C preprocessor, so this file may contain nothing but
 * #defines and comments.
 */
#ifndef THESIS_SHARED_BINDINGS_H
#define THESIS_SHARED_BINDINGS_H

/* Tracing kernels: workloads, tracers and primitive kinds (descriptor set 0). */
#define THESIS_BINDING_RAYS 0 /* ray list, read */
#define THESIS_BINDING_HITS 1 /* hit list, write */

#define THESIS_BINDING_TRACER_FIRST 2 /* 2..6 belong to the tracer itself */
#define THESIS_BINDING_TRACER_LAST 6
#define THESIS_BINDING_HARDWARE_SCENE 2 /* hardware tracer: the GPU-built search structure */
#define THESIS_BINDING_GRID_CELLS 2     /* grid tracer: where the list of each grid cell starts */
#define THESIS_BINDING_GRID_ITEMS 3     /* grid tracer: the primitive indices of all cells */
#define THESIS_BINDING_OCTREE_NODES 2   /* octree tracer: the node words, see Octree.h */
#define THESIS_BINDING_OCTREE_ITEMS 3   /* octree tracer: the lists of cells with several primitives */
#define THESIS_BINDING_BVH_NODES 2      /* BVH tracer: the nodes */

#define THESIS_BINDING_RAY_WORK 7 /* counting variant of a tracer only: one RayWork per ray, write */

#define THESIS_BINDING_PRIMITIVE_BOUNDS 8      /* every primitive kind gets its bounds here */
#define THESIS_BINDING_PRIMITIVE_DATA_FIRST 9  /* 9..15: the primitive kind's own data */
#define THESIS_BINDING_PRIMITIVE_DATA_LAST 15

#define THESIS_BINDING_RAY_LOG 16 /* counting variant of a tracer only: the steps of one ray, see RayLog.h */

/* Primitive kinds brick2012_inline, brick2012_split and their _mip variants (the layouts are in Brick2012.h). */
#define THESIS_BINDING_BRICK_HEADER 9       /* one BrickModelHeader */
#define THESIS_BINDING_BRICK_TABLE 10       /* one BrickRecord per brick */
#define THESIS_BINDING_BRICK_CELLS 11       /* the primary grid cells of all bricks */
#define THESIS_BINDING_BRICK_INDIRECTION 12 /* 16-bit words: the runs of columns with more than one voxel */
#define THESIS_BINDING_BRICK_ATTRIBUTES 13  /* split layout only: one 16-bit normal per voxel */
#define THESIS_BINDING_BRICK_MIPMAP 14      /* encodings with mipmap only: 16-bit min/max mipmap cells */

/* Shading kernel (descriptor set 0). */
#define THESIS_BINDING_SHADE_HITS 0
#define THESIS_BINDING_SHADE_OUTPUT 1
#define THESIS_BINDING_SHADE_WORK 2 /* shade_work only: one RayWork per ray, read */

#endif
