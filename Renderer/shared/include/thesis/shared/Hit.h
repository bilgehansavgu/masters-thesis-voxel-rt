/* Bits of the hit list entries: the ONLY place these numbers are written.
 *
 * Read by the shaders (tracers, primitive kinds, shading) and by C++ (tests, benchmark).
 * Like Bindings.h, this file may contain nothing but #defines and comments.
 *
 * Hit.attributes: what the primitive kind stores for the point that was hit. A tracer reads it
 * once for each ray, after it knows the closest hit.
 *   bits 0..15  the stored surface normal, packed (src/voxel/OctNormal.h, shaders/common/normals.slang)
 *   bit 16      set when the primitive kind stores a normal. The box kind does not.
 *
 * RayWork.hitCell: the kind of primary grid cell in which a ray found its hit. This is the
 * colour code of figure 7 in Reichl et al. 2012.
 */
#ifndef THESIS_SHARED_HIT_H
#define THESIS_SHARED_HIT_H

#define THESIS_HIT_NORMAL_MASK 0xFFFF
#define THESIS_HIT_HAS_NORMAL 0x10000

#define THESIS_HIT_CELL_NONE 0         /* no hit, or the primitive kind has no cells */
#define THESIS_HIT_CELL_HEIGHT 1       /* a height cell: one voxel in the column */
#define THESIS_HIT_CELL_ONE_RUN 2      /* a pointer cell with one run of several voxels */
#define THESIS_HIT_CELL_SEVERAL_RUNS 3 /* a pointer cell with more than one run */

#endif
