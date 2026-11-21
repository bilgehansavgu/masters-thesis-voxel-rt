// BVH tracer: our own bounding volume hierarchy, walked in a compute shader.
//
// The BVH is a binary tree of boxes (Bvh.h). An inner node has two children, a leaf has one
// primitive. The ray starts at the root. At an inner node it tests the boxes of both children.
// It goes on with the child whose box it enters first, and puts the other one on a stack. At a
// leaf it tests the primitive, then takes the next node from the stack. A node whose box
// begins behind the closest hit so far is dropped.
//
// Compiled once per primitive kind; `primitive` resolves to shaders/primitives/<kind>/.

import types;
import aabb;
import closest;
#include "thesis/shared/Bindings.h"
#include "thesis/shared/RayLog.h"
#include "thesis/tracers/software_bvh/BvhShared.h"

// CPU mirror: tracers::BvhNode in Bvh.h.
struct BvhNode
{
    float3 lo;        // the box around everything below this node
    uint   child;     // inner node: its children are the nodes child and child + 1
    float3 hi;
    uint   primitive; // leaf: the primitive. Inner node: THESIS_BVH_NO_PRIMITIVE
};

struct TraceParams
{
    uint rayCount;
    uint pad0;
    uint pad1;
    uint pad2;
};

[[vk::push_constant]] ConstantBuffer<TraceParams> gParams;

[[vk::binding(THESIS_BINDING_RAYS, 0)]] StructuredBuffer<Ray> gRays;
[[vk::binding(THESIS_BINDING_HITS, 0)]] RWStructuredBuffer<Hit> gHits;
[[vk::binding(THESIS_BINDING_BVH_NODES, 0)]] StructuredBuffer<BvhNode> gBvhNodes;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 threadId : SV_DispatchThreadID)
{
    uint rayIndex = threadId.x;
    if (rayIndex >= gParams.rayCount)
        return;

    Ray ray = gRays[rayIndex];
    ClosestHit closest = ClosestHit.none(ray, rayIndex);
    const float3 inverseDirection = safeInverse(ray.direction);

    // Nodes that still have to be visited, with the distance at which the ray enters their box.
    uint  stackNode[THESIS_BVH_STACK_SIZE];
    float stackEntry[THESIS_BVH_STACK_SIZE];
    uint  stackSize = 0;
    // Only entries below stackSize are read, and each of them was written before. The compiler
    // cannot see that. One write that it can see on every path keeps it from refusing the code.
    stackNode[0] = 0;
    stackEntry[0] = kNever;

    // Ray log structure step: a = the index of the node (BvhTracer::describeStructureStep).
    closest.work.structureStep(0, 0, 0, 0);
    BvhNode node = gBvhNodes[0];
    bool walking = boxEntry(ray.origin, inverseDirection, node.lo, node.hi, ray.tMin, closest.t) < kNever;

    while (walking)
    {
        bool takeFromStack = false;
        if (node.primitive != THESIS_BVH_NO_PRIMITIVE)
        {
            closest.test(ray, node.primitive);
            takeFromStack = true;
        }
        else
        {
            closest.work.structureStep(node.child, 0, 0, 0);
            closest.work.structureStep(node.child + 1, 0, 0, 0);
            const BvhNode first = gBvhNodes[node.child];
            const BvhNode second = gBvhNodes[node.child + 1];
            const float entryFirst = boxEntry(ray.origin, inverseDirection, first.lo, first.hi, ray.tMin, closest.t);
            const float entrySecond = boxEntry(ray.origin, inverseDirection, second.lo, second.hi, ray.tMin, closest.t);
            if (entryFirst == kNever && entrySecond == kNever)
            {
                takeFromStack = true;
            }
            else if (entrySecond == kNever)
            {
                node = first;
            }
            else if (entryFirst == kNever)
            {
                node = second;
            }
            else
            {
                // Both boxes are on the way: the nearer one first, the other one later.
                const bool firstIsNearer = entryFirst <= entrySecond;
                stackNode[stackSize] = firstIsNearer ? node.child + 1 : node.child;
                stackEntry[stackSize] = firstIsNearer ? entrySecond : entryFirst;
                stackSize += 1;
                node = firstIsNearer ? first : second;
            }
        }

        if (takeFromStack)
        {
            walking = false;
            while (stackSize > 0)
            {
                stackSize -= 1;
                // A box that begins behind the closest hit cannot hold a closer one.
                if (stackEntry[stackSize] < closest.t)
                {
                    closest.work.structureStep(stackNode[stackSize], 0, 0, 0);
                    node = gBvhNodes[stackNode[stackSize]];
                    walking = true;
                    break;
                }
            }
        }
    }

    gHits[rayIndex] = closest.finish();
    closest.work.store(rayIndex);
}
