/* Bit layout of the 2012 brick format: the ONLY place these numbers are written.
 *
 * Read by the C++ encoder (src/encodings/brick2012) and, from step 2 on, by the brick shaders.
 * Like Bindings.h, this file may contain nothing but #defines and comments.
 *
 * A brick has N x N x N voxels (N = 8, 16 or 32). Its primary grid has N x N cells. Cell (u, v)
 * stands for the column of N voxels along the primary axis `a` of the brick, at position u on
 * axis (a + 1) % 3 and position v on axis (a + 2) % 3. The cell index is u + N * v. A height is
 * a position in the column, 0 .. N - 1. A run is a row of filled voxels in one column.
 *
 * Buffers (bindings in Bindings.h):
 *   header       BrickModelHeader: grid origin, voxel size, N, brick count, layout number
 *   brick table  one BrickRecord per brick: position, primary axis, where its data starts
 *   cells        N * N cells per brick
 *   indirection  16-bit words, see below
 *   attributes   split layout only: 16-bit normals
 *
 * Every cell has its kind in bits 0..1:
 *   EMPTY    no filled voxel in the column
 *   HEIGHT   exactly one filled voxel; bits 2..7 hold its height
 *   POINTER  more than one filled voxel; bits 2.. hold the offset of the column's words in the
 *            indirection buffer, counted from the start of the brick's part of that buffer
 *
 * INLINE layout (the order of Reichl et al. 2012: attributes next to the heights)
 *   cell         32 bits. HEIGHT: bits 8..23 hold the normal of the voxel.
 *   indirection  per POINTER column: its run words, then one normal per voxel, ascending height.
 *
 * SPLIT layout (attributes in their own buffer)
 *   cell         16 bits.
 *   indirection  per brick: N words first, word v = number of HEIGHT cells in the rows before
 *                row v. Then per POINTER column: one word with the index of the column's first
 *                normal, then its run words.
 *   attributes   per brick: the normals of all HEIGHT cells in cell order, then the normals of
 *                the POINTER columns in cell order, ascending height. Indices count from the
 *                start of the brick's part of the buffer.
 *                The normal of HEIGHT cell (u, v) is at index
 *                word v + (number of HEIGHT cells among cells (0, v) .. (u - 1, v)).
 *
 * Run word: bits 0..5 first height, bits 6..11 length - 1, bit 12 set on the last run of the column.
 */
#ifndef THESIS_SHARED_BRICK2012_H
#define THESIS_SHARED_BRICK2012_H

#define THESIS_BRICK_LAYOUT_INLINE 1
#define THESIS_BRICK_LAYOUT_SPLIT 2

#define THESIS_CELL_KIND_MASK 0x3
#define THESIS_CELL_EMPTY 0
#define THESIS_CELL_HEIGHT 1
#define THESIS_CELL_POINTER 2

#define THESIS_CELL_HEIGHT_SHIFT 2
#define THESIS_CELL_HEIGHT_MASK 0x3F
#define THESIS_CELL_NORMAL_SHIFT 8 /* inline layout only */
#define THESIS_CELL_NORMAL_MASK 0xFFFF

#define THESIS_CELL_OFFSET_SHIFT 2
#define THESIS_CELL_OFFSET_MAX_INLINE 0x3FFFFFFF /* 30 bits */
#define THESIS_CELL_OFFSET_MAX_SPLIT 0x3FFF      /* 14 bits */

#define THESIS_RUN_START_MASK 0x3F
#define THESIS_RUN_LENGTH_SHIFT 6
#define THESIS_RUN_LENGTH_MASK 0x3F
#define THESIS_RUN_LAST_BIT 0x1000

/* BrickRecord.position: brick coordinate x in bits 0..9, y in 10..19, z in 20..29, primary axis in 30..31. */
#define THESIS_BRICK_COORDINATE_BITS 10
#define THESIS_BRICK_COORDINATE_MASK 0x3FF
#define THESIS_BRICK_AXIS_SHIFT 30

#endif
