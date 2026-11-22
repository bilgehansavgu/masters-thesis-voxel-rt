// Test shader: where each thread of a tracer-shaped dispatch runs. Same work group as every
// compute shader tracer (tracing::kTraceGroupSize). tests/TestWorkSummary.cpp checks that thread
// i runs in lane i % S of a warp that holds threads S * (i / S) to S * (i / S) + S - 1, which is
// what metrics::RayWorkSummary assumes when it groups the rays of a ray list into warps.

struct WarpLane
{
    uint laneCount;   // WaveGetLaneCount(): threads in this warp
    uint lane;        // WaveGetLaneIndex()
    uint firstThread; // the smallest thread index in this warp
    uint lastThread;  // the largest thread index in this warp
};

[[vk::binding(0, 0)]] RWStructuredBuffer<WarpLane> gLanes;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 threadId : SV_DispatchThreadID)
{
    WarpLane result;
    result.laneCount = WaveGetLaneCount();
    result.lane = WaveGetLaneIndex();
    result.firstThread = WaveActiveMin(threadId.x);
    result.lastThread = WaveActiveMax(threadId.x);
    gLanes[threadId.x] = result;
}
