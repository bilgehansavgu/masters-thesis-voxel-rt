/* Side: Shared */
/* Bits and bindings of the octree tracer: the ONLY place these numbers are written.
 *
 * Read by the C++ builder (src/tracers/software_octree) and by trace_software_octree.slang.
 * Like thesis/shared/Bindings.h, this file may contain nothing but #defines and comments.
 *
 * The octree stands on a cube of 2^depth cells along each side. A node covers a cube of cells.
 * A node with content has eight children, one for each half along x, y and z. The eight
 * children of one node are eight 32-bit words in a row: a tile. The children of the root are
 * tile 0. For the cell (x, y, z), the child that covers 2^L cells along each side is word
 *   (x >> L & 1) + 2 * (y >> L & 1) + 4 * (z >> L & 1)
 * of the tile.
 *
 * A word has its kind in bits 30..31 and a payload in bits 0..29:
 *   EMPTY     no primitive in this cube, whatever its size
 *   INTERIOR  the cube has children: payload = their tile
 *   SINGLE    a cube of one cell with one primitive: payload = the primitive
 *   LIST      a cube of one cell with several primitives: payload = index of the first entry of
 *             its list in the item buffer. An entry is a primitive, with bit 31 set on the last.
 */
#ifndef THESIS_TRACERS_OCTREE_SHARED_H
#define THESIS_TRACERS_OCTREE_SHARED_H

/* Bindings of the tracer itself, inside THESIS_BINDING_TRACER_FIRST .. THESIS_BINDING_TRACER_LAST. */
#define THESIS_BINDING_OCTREE_NODES 2 /* the node words */
#define THESIS_BINDING_OCTREE_ITEMS 3 /* the lists of cells with several primitives */

#define THESIS_OCTREE_KIND_SHIFT 30
#define THESIS_OCTREE_PAYLOAD_MASK 0x3FFFFFFF
#define THESIS_OCTREE_EMPTY 0
#define THESIS_OCTREE_INTERIOR 1
#define THESIS_OCTREE_SINGLE 2
#define THESIS_OCTREE_LIST 3

#define THESIS_OCTREE_ITEM_LAST 0x80000000
#define THESIS_OCTREE_ITEM_MASK 0x7FFFFFFF

#define THESIS_OCTREE_MAX_DEPTH 10 /* 1024 cells along each side */

#endif
