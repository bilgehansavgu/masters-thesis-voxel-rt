#include "thesis/tracers/software_octree/Octree.h"

#include "thesis/tracers/software_octree/OctreeShared.h"

#include <algorithm>
#include <stdexcept>

namespace thesis::tracers {
namespace {

// One primitive in one cell. `code` is the Morton code of the cell: the bits of x, y and z
// interleaved, x lowest. Sorted by code, the entries of every cube of the octree are in a row.
struct Entry {
    std::uint32_t code;
    std::uint32_t primitive;
};

// Spreads the low 10 bits of `value`: bit i goes to bit 3 * i.
std::uint32_t spreadBits(std::uint32_t value)
{
    value &= 0x3FF;
    value = (value | (value << 16)) & 0x030000FF;
    value = (value | (value << 8)) & 0x0300F00F;
    value = (value | (value << 4)) & 0x030C30C3;
    value = (value | (value << 2)) & 0x09249249;
    return value;
}

std::uint32_t mortonCode(std::uint32_t x, std::uint32_t y, std::uint32_t z)
{
    return spreadBits(x) | (spreadBits(y) << 1) | (spreadBits(z) << 2);
}

std::uint32_t word(std::uint32_t kind, std::size_t payload)
{
    if (payload > THESIS_OCTREE_PAYLOAD_MASK) {
        throw std::length_error("buildOctree: the scene needs more nodes than a 30-bit index can name");
    }
    return (kind << THESIS_OCTREE_KIND_SHIFT) | static_cast<std::uint32_t>(payload);
}

class Builder {
public:
    Builder(std::vector<Entry> entries, Octree& octree) : entries_(std::move(entries)), octree_(octree) {}

    // Fills tile `tile`. Its children cover 2^level cells along each side, and the entries
    // [begin, end) are the ones inside the cube of the tile.
    void fillTile(std::size_t tile, std::uint32_t level, std::size_t begin, std::size_t end)
    {
        for (std::uint32_t child = 0; child < 8; ++child) {
            // The three bits of the code at this level are the number of the child.
            std::size_t childEnd = begin;
            while (childEnd < end && ((entries_[childEnd].code >> (3 * level)) & 7u) == child) {
                ++childEnd;
            }
            std::uint32_t value = word(THESIS_OCTREE_EMPTY, 0);
            if (childEnd > begin && level == 0) {
                value = leaf(begin, childEnd);
            } else if (childEnd > begin) {
                const std::size_t childTile = octree_.nodes.size() / 8;
                octree_.nodes.resize(octree_.nodes.size() + 8, word(THESIS_OCTREE_EMPTY, 0));
                value = word(THESIS_OCTREE_INTERIOR, childTile);
                fillTile(childTile, level - 1, begin, childEnd);
            }
            octree_.nodes[tile * 8 + child] = value;
            begin = childEnd;
        }
    }

private:
    // The word for one cell with the primitives of the entries [begin, end).
    std::uint32_t leaf(std::size_t begin, std::size_t end)
    {
        if (end - begin == 1) {
            return word(THESIS_OCTREE_SINGLE, entries_[begin].primitive);
        }
        const std::size_t first = octree_.items.size();
        for (std::size_t entry = begin; entry < end; ++entry) {
            octree_.items.push_back(entries_[entry].primitive | (entry + 1 == end ? THESIS_OCTREE_ITEM_LAST : 0u));
        }
        return word(THESIS_OCTREE_LIST, first);
    }

    std::vector<Entry> entries_;
    Octree& octree_;
};

} // namespace

Octree buildOctree(std::span<const core::Aabb> boxes, const tracing::GridFrame& frame)
{
    if (boxes.size() > THESIS_OCTREE_ITEM_MASK) {
        throw std::length_error("buildOctree: too many primitives");
    }
    Octree octree;
    octree.origin = frame.origin;
    octree.cellSize = frame.cellSize;
    const std::uint32_t longest = std::max({frame.size.x, frame.size.y, frame.size.z});
    while ((1u << octree.depth) < longest) {
        ++octree.depth;
    }
    if (octree.depth > THESIS_OCTREE_MAX_DEPTH) {
        throw std::length_error("buildOctree: the grid has more than 1024 cells along one axis");
    }

    std::vector<Entry> entries;
    entries.reserve(boxes.size());
    for (std::size_t primitive = 0; primitive < boxes.size(); ++primitive) {
        frame.forEachCellOf(boxes[primitive], [&](std::uint32_t x, std::uint32_t y, std::uint32_t z) {
            entries.push_back({mortonCode(x, y, z), static_cast<std::uint32_t>(primitive)});
        });
    }
    // Stable: the primitives of one cell stay in ascending order, as in the uniform grid.
    std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.code < b.code; });

    octree.nodes.assign(8, word(THESIS_OCTREE_EMPTY, 0));
    octree.items.push_back(0); // a GPU buffer cannot be empty; entry 0 is never the start of a list
    const std::size_t count = entries.size();
    Builder(std::move(entries), octree).fillTile(0, octree.depth - 1, 0, count);
    return octree;
}

OctreeLookup lookUp(const Octree& octree, const glm::uvec3& cell)
{
    OctreeLookup result;
    std::size_t tile = 0;
    for (std::uint32_t level = octree.depth; level-- > 0;) {
        const std::uint32_t child =
            ((cell.x >> level) & 1u) | (((cell.y >> level) & 1u) << 1) | (((cell.z >> level) & 1u) << 2);
        const std::uint32_t value = octree.nodes.at(tile * 8 + child);
        const std::uint32_t kind = value >> THESIS_OCTREE_KIND_SHIFT;
        const std::uint32_t payload = value & THESIS_OCTREE_PAYLOAD_MASK;
        result.cubeSize = 1u << level;
        if (kind == THESIS_OCTREE_INTERIOR) {
            tile = payload;
            continue;
        }
        if (kind == THESIS_OCTREE_SINGLE) {
            result.primitives.push_back(payload);
        } else if (kind == THESIS_OCTREE_LIST) {
            for (std::size_t item = payload;; ++item) {
                result.primitives.push_back(octree.items.at(item) & THESIS_OCTREE_ITEM_MASK);
                if ((octree.items.at(item) & THESIS_OCTREE_ITEM_LAST) != 0) {
                    break;
                }
            }
        }
        return result;
    }
    throw std::logic_error("lookUp: the octree has an interior node at the level of single cells");
}

} // namespace thesis::tracers
