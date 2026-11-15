/* ============================================================================
 * READING ORDER: FILE 2 (how C++ and GPU shaders connect their buffers)
 * ============================================================================
 * Descriptor binding slots: the ONLY place a slot number is written.
 *
 * GPU BASICS: What is a "binding slot"?
 * A GPU shader cannot use C++ pointers directly unless given a GPU buffer. Instead, Vulkan uses
 * numbered "descriptor bindings" (like numbered wall sockets: slot 0, slot 1, slot 2, ...).
 * The C++ code plugs a GPU buffer into slot N, and the shader declares a variable at
 * `[[vk::binding(N, 0)]]` to read or write that buffer.
 *
 * Included verbatim by C++ (src/tracing/include/thesis/tracing/Bindings.h and others) and by
 * Slang shaders. Both languages run the C preprocessor, so this file may contain nothing but
 * #defines and C-style comments.
 */
#ifndef THESIS_SHARED_BINDINGS_H
#define THESIS_SHARED_BINDINGS_H

/* Tracing kernels: workloads, tracers and primitive kinds (descriptor set 0).
 * Every tracer shader uses the same slot layout so tracers and primitive kinds can be mixed freely:
 *   - Slot 0: input array of Rays (`StructuredBuffer<Ray>`)
 *   - Slot 1: output array of Hits (`RWStructuredBuffer<Hit>`)
 *   - Slots 2..7: the tracer's own search structure (e.g., slot 2 holds the hardware BVH)
 *   - Slot 8: the array of bounding boxes (`Aabb`) for every primitive in the scene
 *   - Slots 9..15: extra buffers for a primitive kind (later: compressed 2012 voxel brick data)
 */
#define THESIS_BINDING_RAYS 0 /* ray list, read by tracers (written by camera_rays.slang) */
#define THESIS_BINDING_HITS 1 /* hit list, written by tracers */

#define THESIS_BINDING_TRACER_FIRST 2 /* 2..7 belong to the tracer itself */
#define THESIS_BINDING_TRACER_LAST 7
#define THESIS_BINDING_HARDWARE_SCENE 2 /* hardware tracer: the GPU-built search structure (BVH) */

#define THESIS_BINDING_PRIMITIVE_BOUNDS 8     /* every primitive kind gets its AABB bounds here */
#define THESIS_BINDING_PRIMITIVE_DATA_FIRST 9 /* 9..15: the primitive kind's own data */
#define THESIS_BINDING_PRIMITIVE_DATA_LAST 15

/* Shading kernel (descriptor set 0): reads Hits from slot 0 and writes pixel colors to slot 1. */
#define THESIS_BINDING_SHADE_HITS 0
#define THESIS_BINDING_SHADE_OUTPUT 1

#endif
