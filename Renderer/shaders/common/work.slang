// Counts the work of one ray: how many cells or nodes of the search structure it read, how many
// primitives it was tested against, how many cells, mipmap cells and runs it visited inside the
// bricks. For one chosen ray it also writes every step to the ray log (thesis/shared/RayLog.h),
// so the window can show what that ray did (the ray inspector).
//
// Every tracer shader is compiled twice. In the timed variant this struct is empty and every
// call below does nothing, so counting costs no time. The counting variant is compiled with
// -D THESIS_COUNT_WORK and writes one RayWork for each ray. It is for diagnosis only and is
// never timed.

import types;
#include "thesis/shared/Bindings.h"
#include "thesis/shared/RayLog.h"

#ifdef THESIS_COUNT_WORK

[[vk::binding(THESIS_BINDING_RAY_WORK, 0)]]
RWStructuredBuffer<RayWork> gRayWork;

// Entry 0: the header. Entries 1 .. THESIS_RAY_LOG_CAPACITY: the steps of the chosen ray.
[[vk::binding(THESIS_BINDING_RAY_LOG, 0)]]
RWStructuredBuffer<RayLogEntry> gRayLog;

public struct WorkCounter
{
    RayWork work;
    bool logging; // this is the ray that the log is for
    uint logged;  // steps written
    uint lost;    // steps that did not fit

    public static WorkCounter start(uint rayIndex)
    {
        WorkCounter counter;
        counter.work.primitiveTests = 0;
        counter.work.cells = 0;
        counter.work.runs = 0;
        counter.work.hitCell = 0;
        counter.work.structureSteps = 0;
        counter.work.mipmapCells = 0;
        counter.work.mipmapSkips = 0;
        counter.work.skippedLevels = 0;
        counter.logging = gRayLog[0].kind == rayIndex;
        counter.logged = 0;
        counter.lost = 0;
        return counter;
    }

    [mutating] void log(uint kind, uint a, uint b, uint c, uint d, uint e, uint f, float t)
    {
        if (!logging)
            return;
        if (logged >= THESIS_RAY_LOG_CAPACITY)
        {
            lost += 1;
            return;
        }
        logged += 1;
        RayLogEntry entry;
        entry.kind = kind;
        entry.a = a;
        entry.b = b;
        entry.c = c;
        entry.d = d;
        entry.e = e;
        entry.f = f;
        entry.t = t;
        gRayLog[logged] = entry;
    }

    // A cell or node of the tracer's own search structure. a to d: what the tracer says (its shader
    // and ITracer::describeStructureStep).
    [mutating] public void structureStep(uint a, uint b, uint c, uint d)
    {
        work.structureSteps += 1;
        log(THESIS_LOG_STRUCTURE_STEP, a, b, c, d, 0, 0, 0.0);
    }

    [mutating] public void primitiveTest(uint primitive)
    {
        work.primitiveTests += 1;
        log(THESIS_LOG_PRIMITIVE, primitive, 0, 0, 0, 0, 0, 0.0);
    }

    [mutating] public void closerHit(uint primitive, float t)
    {
        log(THESIS_LOG_CLOSER_HIT, primitive, 0, 0, 0, 0, 0, t);
    }

    [mutating] public void result(Hit hit)
    {
        log(THESIS_LOG_RESULT, hit.primitive, hit.face, hit.attributes, 0, 0, 0, hit.t);
    }

    // A cell of the min/max mipmap: the square of 2^level x 2^level cells from `first`, with its
    // lowest and highest filled voxel. `skipped`: the ray passes by, the walk skips the square.
    [mutating] public void mipmapCell(uint level, uint2 first, uint lowest, uint highest, bool skipped, float t)
    {
        work.mipmapCells += 1;
        if (skipped)
        {
            work.mipmapSkips += 1;
            work.skippedLevels |= 1u << level;
        }
        log(THESIS_LOG_MIPMAP, level, first.x, first.y, lowest, highest, skipped ? 1 : 0, t);
    }

    // A cell of the primary grid of a brick. For a height cell, `bottom` and `top` give its voxel.
    [mutating] public void cell(uint2 position, uint kind, uint bottom, uint top, float t)
    {
        work.cells += 1;
        log(THESIS_LOG_CELL, position.x, position.y, kind, bottom, top, 0, t);
    }

    [mutating] public void run(uint first, uint length, bool met, float t)
    {
        work.runs += 1;
        log(THESIS_LOG_RUN, first, length, met ? 1 : 0, 0, 0, 0, t);
    }

    [mutating] public void voxelHit(uint2 position, uint height, uint face, float t)
    {
        log(THESIS_LOG_VOXEL_HIT, position.x, position.y, height, face, 0, 0, t);
    }

    [mutating] public void hitCell(uint kind) { work.hitCell = kind; }

    public void store(uint rayIndex)
    {
        gRayWork[rayIndex] = work;
        if (logging)
        {
            gRayLog[0].a = logged;
            gRayLog[0].b = lost;
        }
    }
};

#else

public struct WorkCounter
{
    public static WorkCounter start(uint rayIndex)
    {
        WorkCounter counter;
        return counter;
    }

    [mutating] public void structureStep(uint a, uint b, uint c, uint d) {}
    [mutating] public void primitiveTest(uint primitive) {}
    [mutating] public void closerHit(uint primitive, float t) {}
    [mutating] public void result(Hit hit) {}
    [mutating] public void mipmapCell(uint level, uint2 first, uint lowest, uint highest, bool skipped, float t) {}
    [mutating] public void cell(uint2 position, uint kind, uint bottom, uint top, float t) {}
    [mutating] public void run(uint first, uint length, bool met, float t) {}
    [mutating] public void voxelHit(uint2 position, uint height, uint face, float t) {}
    [mutating] public void hitCell(uint kind) {}

    public void store(uint rayIndex) {}
};

#endif
