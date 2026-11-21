/* Side: Shared */
/* What the C++ side and the shader of the grid tracer both know: the ONLY place these numbers are
 * written. Read by GridTracer.cpp and trace_software_grid.slang. Like thesis/shared/Bindings.h,
 * this file may contain nothing but #defines and comments.
 */
#ifndef THESIS_TRACERS_GRID_SHARED_H
#define THESIS_TRACERS_GRID_SHARED_H

/* Bindings of the tracer itself, inside THESIS_BINDING_TRACER_FIRST .. THESIS_BINDING_TRACER_LAST. */
#define THESIS_BINDING_GRID_CELLS 2 /* where the list of each grid cell starts */
#define THESIS_BINDING_GRID_ITEMS 3 /* the primitive indices of all cells */

#endif
