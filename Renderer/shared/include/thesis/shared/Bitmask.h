/* Side: Shared */
/* Bit layout of the bitmask brick format: the ONLY place these numbers are written.
 *
 * The baseline that the 2012 format is measured against: each brick stores one occupancy bit for
 * every one of its N x N x N voxels, whether filled or empty, and one normal per filled voxel. It is
 * the voxel model's own form (thesis/voxel/VoxelModel.h) made readable for a shader. Read by the
 * C++ encoder (src/encodings/bitmask) and by shaders/primitives/bitmask.
 * WHY in shared/: the primitive kind's shader is compiled inside every tracer's folder, so the
 * encoder's own include/ folder is not on its search path. Like Bindings.h, this file may contain
 * nothing but #defines and comments.
 *
 * Columns run along x. Cell (u, v) of a brick is the column of N voxels at y = u and z = v, cell
 * index u + N * v, height = x. Bit `height` of column `cell` is voxel bit N * cell + height of the
 * voxel model, so the masks are the occupancy bits of the voxel model, unchanged.
 *
 * Buffers (bindings in Bindings.h):
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
#ifndef THESIS_SHARED_BITMASK_H
#define THESIS_SHARED_BITMASK_H

/* BitmaskBrickRecord.position: brick coordinate x in bits 0..9, y in 10..19, z in 20..29. */
#define THESIS_BITMASK_COORDINATE_BITS 10
#define THESIS_BITMASK_COORDINATE_MASK 0x3FF

#endif
