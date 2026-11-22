#pragma once

// Side: CPU

#include "thesis/resolve/ColorScale.h"

#include <array>
#include <cstdint>

namespace thesis::resolve {

enum class ShadingMode : std::uint32_t {
    Shaded = 0,      // lighting with the stored normal; a box has none and gets the normal of its face
    Distance = 1,    // hit distance as grey (near = white)
    PrimitiveId = 2, // flat colour per primitive (for a voxel model: per brick)
    Normal = 3,      // the stored normal as colour
    Faces = 4,       // lighting with the normal of the voxel face that was hit: shows the voxels
    // The views below show the work of each ray. They need the counting variant of a tracer.
    WorkCells = 5,      // cells of primary grids the ray visited
    WorkPrimitives = 6, // primitives the ray was tested against
    WorkRuns = 7,       // runs the ray was tested against
    WorkHitCell = 8,    // kind of the cell with the hit (figure 7 of Reichl et al. 2012)
    WorkStructure = 9,  // cells or nodes of the tracer's own search structure the ray read
    WorkMipmap = 10,    // cells of the min/max mipmaps of the bricks the ray read
    WorkSkipLevel = 11, // the largest mipmap square that the walk skipped: one colour for each level
    WorkCycles = 12,    // GPU clock cycles of the ray, in the counting variant (core::RayWork::cycles)
};

inline constexpr std::array<const char*, 13> kShadingModeNames{"Shaded",
                                                               "Distance",
                                                               "Primitive id",
                                                               "Stored normal",
                                                               "Voxel faces",
                                                               "Work: cells visited",
                                                               "Work: primitives tested",
                                                               "Work: runs tested",
                                                               "Work: cell kind at the hit",
                                                               "Work: search structure steps",
                                                               "Work: mipmap cells read",
                                                               "Work: largest mipmap square skipped",
                                                               "Work: GPU clock cycles"};

// True for the views that show the work counters.
[[nodiscard]] constexpr bool showsWork(ShadingMode mode)
{
    return mode >= ShadingMode::WorkCells;
}

// True for the work views that show a count on the colour scale (ColorScale.h). The other two
// work views show categories.
[[nodiscard]] constexpr bool showsCount(ShadingMode mode)
{
    return showsWork(mode) && mode != ShadingMode::WorkHitCell && mode != ShadingMode::WorkSkipLevel;
}

// The count at the end of the colour map of a count view before anyone sets it: clock cycles
// run to tens of thousands per ray, the other counters to tens.
[[nodiscard]] constexpr float defaultScaleMaximum(ShadingMode mode)
{
    return mode == ShadingMode::WorkCycles ? 20000.0f : 64.0f;
}

// What a count view counts, as the title of its colour bar. nullptr for the other views.
[[nodiscard]] constexpr const char* countedQuantity(ShadingMode mode)
{
    switch (mode) {
    case ShadingMode::WorkCells:
        return "Primary grid cells visited per ray";
    case ShadingMode::WorkPrimitives:
        return "Primitives tested per ray";
    case ShadingMode::WorkRuns:
        return "Runs tested per ray";
    case ShadingMode::WorkStructure:
        return "Search structure steps per ray";
    case ShadingMode::WorkMipmap:
        return "Mipmap cells read per ray";
    case ShadingMode::WorkCycles:
        return "GPU clock cycles per ray";
    default:
        return nullptr;
    }
}

// The direction towards the light, used by the lit views (Shaded and Faces). Angles in radians.
// Azimuth turns around the up axis (y), 0 points towards +z. Elevation is the angle above the
// horizontal plane. The defaults give the direction (0.4, 1, 0.3) that the renderer always used.
struct LightDirection {
    float azimuth = 0.9273f;   // atan2(0.4, 0.3) = 53.13 deg
    float elevation = 1.1071f; // atan(1 / 0.5) = 63.43 deg
};

// True for the views that use the light.
[[nodiscard]] constexpr bool usesLight(ShadingMode mode)
{
    return mode == ShadingMode::Shaded || mode == ShadingMode::Faces;
}

struct ShadingParams {
    ShadingMode mode = ShadingMode::Shaded;
    float distanceNear = 0.0f;
    float distanceFar = 50.0f;
    ColorScale workScale; // count views: how a count becomes a colour
    LightDirection light; // lit views
    // Count views: 0 shows the count of each ray. Otherwise the rays are grouped in warps of this
    // many neighbours of the ray list, and each pixel shows the largest count of its warp: the
    // warp is busy until its slowest ray is done.
    std::uint32_t warpSize = 0;
};

} // namespace thesis::resolve
