/* The ray log: the steps of ONE ray, in the order in which the tracer took them. The ONLY place
 * these numbers are written.
 *
 * Only the counting variant of a tracer writes it (shaders/common/work.slang), and only for the
 * ray that the user picked in the window (the ray inspector) or that a test picked. Read by the
 * shaders and by C++ (src/app/RayLogText.cpp turns it into text). Like Bindings.h, this file may
 * contain nothing but #defines and comments.
 *
 * The buffer at THESIS_BINDING_RAY_LOG holds entries of 8 words (RayLogEntry in types.slang and
 * core/GpuTypes.h: kind, a, b, c, d, e, f, t).
 *   Entry 0 is the header. The CPU writes kind = the index of the ray to log
 *   (THESIS_LOG_NO_RAY: none) and sets a and b to 0. The shader writes a = the number of steps
 *   it wrote, b = the number of steps that did not fit.
 *   Entries 1 .. THESIS_RAY_LOG_CAPACITY: one step each. t is the distance along the ray at
 *   which the step begins, or the distance of the hit.
 *
 * Coordinates inside a brick are cells of its primary grid: u and v, from 0 to N-1. u is the
 * axis after the height axis w (x -> y -> z -> x), v the one after u.
 */
#ifndef THESIS_SHARED_RAY_LOG_H
#define THESIS_SHARED_RAY_LOG_H

#define THESIS_RAY_LOG_CAPACITY 4095 /* steps; with the header 4096 entries, 128 KiB */
#define THESIS_LOG_NO_RAY 0xFFFFFFFF

/* Steps through the search structure of the tracer. The hardware tracer writes none: the GPU
 * does not tell which nodes of its BVH a ray visits. */
#define THESIS_LOG_GRID_CELL 1   /* a, b, c: the cell (x, y, z) of the uniform grid */
#define THESIS_LOG_OCTREE_NODE 2 /* a, b, c: the cell of the octree that the ray is in; d: level of the node (its cube has 2^d cells along each side) */
#define THESIS_LOG_BVH_NODE 3    /* a: the index of the node */

/* The primitives. */
#define THESIS_LOG_PRIMITIVE 4   /* a: the primitive (for a voxel model: the brick) that the ray is tested against */
#define THESIS_LOG_CLOSER_HIT 5  /* a: the primitive; t: the distance of its hit. It is the closest hit so far. */
#define THESIS_LOG_RESULT 6      /* the end: a: primitive or 0xFFFFFFFF (miss), b: face, c: attributes; t: distance */

/* Steps inside a brick. */
#define THESIS_LOG_MIPMAP 7      /* a: level L; b, c: the first cell (u, v) of the square of 2^L x 2^L cells;
                                    d: lowest, e: highest filled voxel of the square (d > e: empty);
                                    f: 1 the ray passes by and the walk skips the square, 0 the walk goes one level down */
#define THESIS_LOG_CELL 8        /* a, b: the cell (u, v); c: its kind (THESIS_CELL_* in Brick2012.h);
                                    d, e: for a height cell, the voxel from d to e (e = d + 1) */
#define THESIS_LOG_RUN 9         /* a: the first voxel of the run, b: its number of voxels, c: 1 if the ray meets it */
#define THESIS_LOG_VOXEL_HIT 10  /* a, b: the cell (u, v); c: the height of the voxel; d: the face (0..5); t: distance */

#endif
