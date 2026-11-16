/* ============================================================================
 * READING ORDER: FILE 8
 * ============================================================================
 * PURPOSE:
 * Defines the exact bit-level layout of the 2012 brick format (Reichl et al. 2012)
 * in a single shared header included by both C++ (`Brick2012Encoding.cpp`) and
 * Slang GPU shaders (`shaders/primitives/brick2012_*/primitive.slang`).
 *
 * Like `Bindings.h`, this file contains ONLY `#define` macros and `/* ... */`
 * comments so both the C++20 compiler and Slang shader compiler can preprocess it.
 *
 * ----------------------------------------------------------------------------
 * VISUALIZING THE BIT LAYOUTS
 * ----------------------------------------------------------------------------
 * A brick has N x N x N voxels (N = 8, 16 or 32). Its primary grid has N x N cells.
 * Cell (u, v) stands for the column of N voxels along the primary axis `a` of the
 * brick, at position u on axis (a + 1) % 3 and position v on axis (a + 2) % 3.
 * The cell index is `u + N * v`. A height is a position in the column, 0 .. N - 1.
 * A run is a contiguous segment of filled voxels in one column.
 *
 * Buffers (bindings in Bindings.h):
 *   header       BrickModelHeader: grid origin, voxel size, N, brick count, layout number
 *   brick table  one BrickRecord per brick: position, primary axis, where its data starts
 *   cells        N * N cells per brick
 *   indirection  16-bit words, see below
 *   attributes   split layout only: 16-bit normals
 *
 * Every cell has its kind in bits 0..1 (`cell & 0x3`):
 *   0 = EMPTY    no filled voxel in the column
 *   1 = HEIGHT   exactly one filled voxel; bits 2..7 hold its height (0..N-1)
 *   2 = POINTER  more than one filled voxel; bits 2.. hold the offset of the column's
 *                words in the indirection buffer, counted from the start of the brick's
 *                part of that buffer (`BrickRecord.indirectionBase`)
 *
 * ----------------------------------------------------------------------------
 * LAYOUT 1: INLINE (`brick2012_inline`, Reichl et al. 2012)
 * ----------------------------------------------------------------------------
 * Normals live right next to heights:
 *   - Cell is 32 bits:
 *       EMPTY:   [ 00..00 | 00 (2b) ]
 *       HEIGHT:  [ unused (8b) | normal (16b, bits 8..23) | height (6b, bits 2..7) | 01 (2b) ]
 *       POINTER: [ indirection word offset (30b, bits 2..31)                       | 10 (2b) ]
 *   - Indirection buffer (per POINTER column):
 *       [ run_0 (16b) ] ... [ run_k (16b, bit 12 = 1) ] [ normal_0 (16b) ] ... [ normal_m (16b) ]
 *
 * ----------------------------------------------------------------------------
 * LAYOUT 2: SPLIT (`brick2012_split`)
 * ----------------------------------------------------------------------------
 * Geometry (heights/runs) is separated from shading attributes (normals) so that
 * shadow rays or primary ray traversal never fetch normal bytes until a final hit!
 *   - Cell is 16 bits (half the size of inline!):
 *       EMPTY:   [ 00..00 | 00 (2b) ]
 *       HEIGHT:  [ unused (8b) | height (6b, bits 2..7) | 01 (2b) ]
 *       POINTER: [ indirection word offset (14b, bits 2..15) | 10 (2b) ]
 *   - Indirection buffer (per brick):
 *       First N words: `rowPrefix[v]` = number of HEIGHT cells in rows 0 .. v-1.
 *       Then per POINTER column:
 *         [ firstNormalIndex (16b) ] [ run_0 (16b) ] ... [ run_k (16b, bit 12 = 1) ]
 *   - Attributes buffer (per brick):
 *       First all HEIGHT-cell normals in cell order, then all POINTER-column normals.
 *       To find the normal of HEIGHT cell (u, v) without storing an index inside the
 *       16-bit cell: start with `rowPrefix[v]` from the indirection buffer, and add
 *       the number of HEIGHT cells earlier in row `v` (columns `0 .. u - 1`).
 *
 * ----------------------------------------------------------------------------
 * 16-BIT RUN WORD
 * ----------------------------------------------------------------------------
 *   bits 0..5  (6 bits): `start` height (0 .. 63)
 *   bits 6..11 (6 bits): `length - 1` (so length 1..64 fits in 0..63)
 *   bit  12    (1 bit) : `THESIS_RUN_LAST_BIT` (1 on the last run of the column)
 * ============================================================================
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
