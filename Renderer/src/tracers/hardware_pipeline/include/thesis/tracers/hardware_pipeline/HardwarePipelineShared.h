/* Side: Shared */
/* What the C++ side and the shaders of the hardware pipeline tracers both know: the ONLY place these
 * numbers are written. Read by HardwarePipelineTracer.cpp and the shaders in ../../../../shaders/.
 * Like thesis/shared/Bindings.h, this file may contain nothing but #defines and comments.
 */
#ifndef THESIS_TRACERS_HARDWARE_PIPELINE_SHARED_H
#define THESIS_TRACERS_HARDWARE_PIPELINE_SHARED_H

/* Bindings of the tracer itself, inside THESIS_BINDING_TRACER_FIRST .. THESIS_BINDING_TRACER_LAST. */
#define THESIS_BINDING_HARDWARE_PIPELINE_SCENE 2 /* the GPU-built search structure */

#endif
