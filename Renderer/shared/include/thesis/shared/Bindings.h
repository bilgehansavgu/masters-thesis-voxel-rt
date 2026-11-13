/* Descriptor binding slots: the ONLY place a slot number is written.
 *
 * Included verbatim by C++ (src/tracing/include/thesis/tracing/Bindings.h and others) and by
 * Slang shaders. Both languages run the C preprocessor, so this file may contain nothing but
 * #defines and comments.
 */
#ifndef THESIS_SHARED_BINDINGS_H
#define THESIS_SHARED_BINDINGS_H

/* Tracing kernels: workloads, tracers and primitive kinds (descriptor set 0). */
#define THESIS_BINDING_RAYS 0 /* ray list, read */
#define THESIS_BINDING_HITS 1 /* hit list, write */

#define THESIS_BINDING_TRACER_FIRST 2 /* 2..7 belong to the tracer itself */
#define THESIS_BINDING_TRACER_LAST 7
#define THESIS_BINDING_HARDWARE_SCENE 2 /* hardware tracer: the GPU-built search structure */

#define THESIS_BINDING_PRIMITIVE_BOUNDS 8      /* every primitive kind gets its bounds here */
#define THESIS_BINDING_PRIMITIVE_DATA_FIRST 9  /* 9..15: the primitive kind's own data */
#define THESIS_BINDING_PRIMITIVE_DATA_LAST 15

/* Shading kernel (descriptor set 0). */
#define THESIS_BINDING_SHADE_HITS 0
#define THESIS_BINDING_SHADE_OUTPUT 1

#endif
