#pragma once

// CPU-side profiling markers. They show up on the Tracy timeline when the Tracy profiler is
// connected and cost almost nothing when it is not (Tracy is built in on-demand mode).
// GPU-side markers live in thesis/gpu/GpuTimer.h because they need a command buffer.

#include <tracy/Tracy.hpp>

#define THESIS_ZONE(name) ZoneScopedN(name)
#define THESIS_FRAME_MARK() FrameMark
// `name` must be a string with static lifetime: Tracy identifies a plot by the pointer.
#define THESIS_PLOT(name, value) TracyPlot(name, value)
#define THESIS_MESSAGE(text, size) TracyMessage(text, size)
