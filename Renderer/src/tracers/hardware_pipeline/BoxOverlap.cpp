#include "thesis/tracers/hardware_pipeline/BoxOverlap.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace thesis::tracers {
namespace {

bool insidesOverlap(const core::Aabb& a, const core::Aabb& b)
{
    return a.lo.x < b.hi.x && b.lo.x < a.hi.x && a.lo.y < b.hi.y && b.lo.y < a.hi.y && a.lo.z < b.hi.z &&
           b.lo.z < a.hi.z;
}

} // namespace

std::optional<BoxOverlap> findBoxOverlap(std::span<const core::Aabb> boxes, const tracing::GridFrame& frame)
{
    // (cell, box) for every cell a box belongs to, sorted by cell: the boxes of one cell are then
    // next to each other. Two boxes that overlap share at least one cell.
    // WHY not frame.oneCellEach alone: that hint says each box lies inside one cell, so boxes in
    // different cells cannot overlap, but two boxes in the same cell still can. Comparing within
    // cells covers both kinds of frame.
    std::vector<std::pair<std::uint64_t, std::uint32_t>> entries;
    entries.reserve(boxes.size());
    for (std::uint32_t index = 0; index < boxes.size(); ++index) {
        frame.forEachCellOf(boxes[index], [&](std::uint32_t x, std::uint32_t y, std::uint32_t z) {
            const std::uint64_t cell = (static_cast<std::uint64_t>(z) * frame.size.y + y) * frame.size.x + x;
            entries.emplace_back(cell, index);
        });
    }
    std::sort(entries.begin(), entries.end());

    for (std::size_t begin = 0; begin < entries.size();) {
        std::size_t end = begin + 1;
        while (end < entries.size() && entries[end].first == entries[begin].first) {
            ++end;
        }
        for (std::size_t i = begin; i < end; ++i) {
            for (std::size_t j = i + 1; j < end; ++j) {
                if (insidesOverlap(boxes[entries[i].second], boxes[entries[j].second])) {
                    return BoxOverlap{entries[i].second, entries[j].second};
                }
            }
        }
        begin = end;
    }
    return std::nullopt;
}

} // namespace thesis::tracers
