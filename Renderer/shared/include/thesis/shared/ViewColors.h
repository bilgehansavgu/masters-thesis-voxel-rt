/* Side: Shared */
/* The fixed colours of the image views: the ONLY place these numbers are written.
 *
 * Read by the shading shaders (shade.slang, shade_work.slang, display.slang) and by
 * resolve/ViewLegend.h, which draws the legend of each view in the window and in screenshots.
 * So a legend shows exactly the colours of the image.
 *
 * Colours here are linear, like every colour a shader computes. The image stores them
 * display-encoded: value^(1 / THESIS_DISPLAY_GAMMA) (toDisplay() in display.slang).
 *
 * Like Bindings.h, this file may contain nothing but #defines and comments.
 */
#ifndef THESIS_SHARED_VIEW_COLORS_H
#define THESIS_SHARED_VIEW_COLORS_H

/* Linear colour -> stored image value: value^(1 / THESIS_DISPLAY_GAMMA). */
#define THESIS_DISPLAY_GAMMA 2.2

/* The background where a ray hits nothing: a vertical gradient from the top to the bottom row. */
#define THESIS_COLOR_SKY_TOP 0.55, 0.70, 0.90
#define THESIS_COLOR_SKY_BOTTOM 0.22, 0.27, 0.36

/* "Cell kind at the hit": the colours of figure 7 in Reichl et al. 2012 (RayWork.hitCell, Hit.h). */
#define THESIS_COLOR_HIT_HEIGHT 0.15, 0.70, 0.20       /* green: a height cell, one voxel in the column */
#define THESIS_COLOR_HIT_ONE_RUN 0.15, 0.30, 0.90      /* blue: one run of several voxels */
#define THESIS_COLOR_HIT_SEVERAL_RUNS 0.90, 0.15, 0.10 /* red: more than one run */
#define THESIS_COLOR_HIT_NO_CELLS 0.50, 0.50, 0.50     /* grey: a hit on a primitive kind without cells */

/* "Largest mipmap square skipped": one colour for each mipmap level 1 to 6 (squares of 2x2 to
 * 64x64 cells). Black: no square skipped. */
#define THESIS_MIPMAP_LEVELS 6
#define THESIS_COLOR_LEVEL_1 0.20, 0.40, 0.95 /* 2x2 */
#define THESIS_COLOR_LEVEL_2 0.15, 0.75, 0.30 /* 4x4 */
#define THESIS_COLOR_LEVEL_3 0.95, 0.85, 0.10 /* 8x8 */
#define THESIS_COLOR_LEVEL_4 0.95, 0.45, 0.10 /* 16x16 */
#define THESIS_COLOR_LEVEL_5 0.85, 0.15, 0.15 /* 32x32 */
#define THESIS_COLOR_LEVEL_6 0.80, 0.30, 0.85 /* 64x64 */

#endif
