/* Side: Shared */
/* Bit layout and bindings of the bitmask brick format: the ONLY place these numbers are written.
 *
 * The baseline that the 2012 format is measured against: each brick stores one occupancy bit for
 * every one of its N x N x N voxels, whether filled or empty, and one normal per filled voxel. It is
 * the voxel model's own form (thesis/voxel/VoxelModel.h) made readable for a shader. Read by the
 * C++ encoder (BitmaskEncoding.cpp) and by shaders/primitives/bitmask. Like
 * thesis/shared/Bindings.h, this file may contain nothing but #defines and comments.
 *
 * Columns run along x. Cell (u, v) of a brick is the column of N voxels at y = u and z = v, cell
 * index u + N * v, height = x. Bit `height` of column `cell` is voxel bit N * cell + height of the
 * voxel model, so the masks are the occupancy bits of the voxel model, unchanged.
 *
 * Buffers (bindings below):
 *   header      BitmaskModelHeader: grid origin, voxel size, N, brick count, voxel count
 *   brick table one BitmaskBrickRecord per brick: brick coordinate, index of its first normal
 *   masks       32-bit words, N * N * N / 32 per brick, in brick order. Column `cell` of brick b
 *               is bits (N * cell) % 32 .. + N - 1 of word b * N * N * N / 32 + N * cell / 32.
 *   row counts  16-bit words, N per brick, in brick order. Word v of a brick: the number of filled
 *               voxels in the rows of cells before row v (the cells u + N * v' with v' < v).
 *   normals     16-bit packed normals, one per filled voxel, per brick in cell order and ascending
 *               height (the voxel bit order). The normal of voxel `height` in cell (u, v) is at
 *               normalBase + row count v + filled voxels of cells (0, v) .. (u - 1, v)
 *               + filled voxels below `height` in the cell.
 */
#ifndef THESIS_BITMASK_SHARED_H
#define THESIS_BITMASK_SHARED_H

/* Bindings of the primitive kind, inside THESIS_BINDING_PRIMITIVE_DATA_FIRST .. _LAST. */
#define THESIS_BINDING_BITMASK_HEADER 9      /* one BitmaskModelHeader */
#define THESIS_BINDING_BITMASK_TABLE 10      /* one BitmaskBrickRecord per brick */
#define THESIS_BINDING_BITMASK_MASKS 11      /* one occupancy bit per voxel */
#define THESIS_BINDING_BITMASK_ROW_COUNTS 12 /* 16-bit words: filled voxels before each row of cells */
#define THESIS_BINDING_BITMASK_NORMALS 13    /* 16-bit words: one normal per filled voxel */

/* BitmaskBrickRecord.position: brick coordinate x in bits 0..9, y in 10..19, z in 20..29. */
#define THESIS_BITMASK_COORDINATE_BITS 10
#define THESIS_BITMASK_COORDINATE_MASK 0x3FF

#endif
