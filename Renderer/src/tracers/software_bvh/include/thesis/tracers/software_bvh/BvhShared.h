/* Side: Shared */
/* Numbers of the BVH of the BVH tracer: the ONLY place they are written.
 *
 * Read by the C++ builder (src/tracers/software_bvh) and by trace_software_bvh.slang.
 * Like thesis/shared/Bindings.h, this file may contain nothing but #defines and comments.
 */
#ifndef THESIS_TRACERS_BVH_SHARED_H
#define THESIS_TRACERS_BVH_SHARED_H

/* Bindings of the tracer itself, inside THESIS_BINDING_TRACER_FIRST .. THESIS_BINDING_TRACER_LAST. */
#define THESIS_BINDING_BVH_NODES 2 /* the nodes */

/* BvhNode.primitive of an inner node. */
#define THESIS_BVH_NO_PRIMITIVE 0xFFFFFFFF

/* Entries of the stack that the shader keeps for each ray. The walk goes to the nearer child
 * first and puts the other one on the stack, so it needs at most one entry for each level of
 * the tree. The tracer refuses a tree that is deeper. */
#define THESIS_BVH_STACK_SIZE 64

#endif
