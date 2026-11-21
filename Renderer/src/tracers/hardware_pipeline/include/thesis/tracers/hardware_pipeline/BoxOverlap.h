#pragma once

// Side: CPU

#include "thesis/core/GpuTypes.h"
#include "thesis/tracing/GridFrame.h"

#include <cstdint>
#include <optional>
#include <span>

namespace thesis::tracers {

// Two primitive boxes whose insides overlap.
struct BoxOverlap {
    std::uint32_t first;
    std::uint32_t second;
};

// The first pair of boxes whose insides overlap, or none. Boxes that only touch (share a face,
// an edge or a corner) do not overlap. `frame` sorts the boxes into cells, so only boxes in the
// same cell are compared; with a frame from chooseGridFrame this is about linear in the box count.
[[nodiscard]] std::optional<BoxOverlap> findBoxOverlap(std::span<const core::Aabb> boxes,
                                                       const tracing::GridFrame& frame);

} // namespace thesis::tracers
