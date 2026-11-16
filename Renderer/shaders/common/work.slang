// Counts the work of one ray: how many primitives it was tested against, how many cells and
// runs it visited inside the bricks.
//
// Every tracer shader is compiled twice. In the timed variant this struct is empty and every
// call below does nothing, so counting costs no time. The counting variant is compiled with
// -D THESIS_COUNT_WORK and writes one RayWork for each ray. It is for diagnosis only and is
// never timed.

import types;
#include "thesis/shared/Bindings.h"

#ifdef THESIS_COUNT_WORK

[[vk::binding(THESIS_BINDING_RAY_WORK, 0)]]
RWStructuredBuffer<RayWork> gRayWork;

public struct WorkCounter
{
    RayWork work;

    public static WorkCounter start()
    {
        WorkCounter counter;
        counter.work.primitiveTests = 0;
        counter.work.cells = 0;
        counter.work.runs = 0;
        counter.work.hitCell = 0;
        return counter;
    }

    [mutating] public void primitiveTest() { work.primitiveTests += 1; }
    [mutating] public void cell() { work.cells += 1; }
    [mutating] public void run() { work.runs += 1; }
    [mutating] public void hitCell(uint kind) { work.hitCell = kind; }

    public void store(uint rayIndex) { gRayWork[rayIndex] = work; }
};

#else

public struct WorkCounter
{
    public static WorkCounter start()
    {
        WorkCounter counter;
        return counter;
    }

    [mutating] public void primitiveTest() {}
    [mutating] public void cell() {}
    [mutating] public void run() {}
    [mutating] public void hitCell(uint kind) {}

    public void store(uint rayIndex) {}
};

#endif
