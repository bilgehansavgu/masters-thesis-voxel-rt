/* Side: Shared */
/* Where the ray of pixel (x, y) sits in the ray list: the ONLY place this rule is written.
 *
 * Read by the camera ray workload (writes the rays), the shading shaders (read the hits back into
 * an image) and C++ (picks the ray of a pixel). Tracers never use it: for them the list is flat.
 * Like Bindings.h, this file may contain nothing but #defines and comments.
 *
 * A GPU runs 32 neighbouring threads of a list together (a warp). The order decides which 32
 * pixels that is:
 *   ROWS   row by row: one warp is 32 pixels of one row.
 *   TILES  bands of 4 rows; inside a band, blocks of 8 columns, each block row by row. One warp
 *          is a tile of 8 x 4 pixels, whose rays point in more similar directions. At the right
 *          and bottom edge the blocks are narrower or lower, so every image size works.
 */
#ifndef THESIS_SHARED_RAY_ORDER_H
#define THESIS_SHARED_RAY_ORDER_H

#define THESIS_RAY_ORDER_ROWS 0
#define THESIS_RAY_ORDER_TILES 1

#define THESIS_TILE_WIDTH 8u
#define THESIS_TILE_HEIGHT 4u

#define THESIS_RAY_ORDER_MIN(a, b) ((a) < (b) ? (a) : (b))

/* The first row of the band of row y, and the first column of the block of column x. */
#define THESIS_TILE_BAND(y) ((y) - (y) % THESIS_TILE_HEIGHT)
#define THESIS_TILE_BLOCK(x) ((x) - (x) % THESIS_TILE_WIDTH)

/* The index in the ray list of pixel (x, y) of a width x height image. All values unsigned. */
#define THESIS_RAY_INDEX(order, x, y, width, height)                                                             \
    ((order) == THESIS_RAY_ORDER_TILES                                                                           \
         ? THESIS_TILE_BAND(y) * (width) +                                                                       \
               THESIS_TILE_BLOCK(x) * THESIS_RAY_ORDER_MIN(THESIS_TILE_HEIGHT, (height) - THESIS_TILE_BAND(y)) + \
               ((y) - THESIS_TILE_BAND(y)) * THESIS_RAY_ORDER_MIN(THESIS_TILE_WIDTH, (width) - THESIS_TILE_BLOCK(x)) + \
               ((x) - THESIS_TILE_BLOCK(x))                                                                      \
         : (y) * (width) + (x))

#endif
