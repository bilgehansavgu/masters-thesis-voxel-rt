/* Side: Shared */
/* Descriptor binding slots of the sockets: the ONLY place these slot numbers are written. The
 * slots that belong to one tracer (2..6) or one primitive kind (9..15) are written beside that
 * plugin's other numbers.
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

/* 2..6 belong to the tracer itself. Each tracer names its own in its folder:
 * src/tracers/<name>/include/thesis/tracers/<name>/<Name>Shared.h. */
#define THESIS_BINDING_TRACER_FIRST 2
#define THESIS_BINDING_TRACER_LAST 6

#define THESIS_BINDING_RAY_WORK 7 /* counting variant of a tracer only: one RayWork per ray, write */

#define THESIS_BINDING_PRIMITIVE_BOUNDS 8 /* every primitive kind gets its bounds here */

/* 9..15: the primitive kind's own data. Each kind names its own beside its bits: in the header
 * that its encoder and its primitive.slang share. */
#define THESIS_BINDING_PRIMITIVE_DATA_FIRST 9
#define THESIS_BINDING_PRIMITIVE_DATA_LAST 15

#define THESIS_BINDING_RAY_LOG 16 /* counting variant of a tracer only: the steps of one ray, see RayLog.h */

/* Shading kernel (descriptor set 0). */
#define THESIS_BINDING_SHADE_HITS 0
#define THESIS_BINDING_SHADE_OUTPUT 1
#define THESIS_BINDING_SHADE_WORK 2 /* shade_work only: one RayWork per ray, read */

#endif
