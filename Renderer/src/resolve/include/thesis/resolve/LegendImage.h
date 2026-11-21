#pragma once

// Side: CPU

#include "thesis/resolve/ViewLegend.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace thesis::resolve {

// An image in memory: RGBA8, rows from the top, display-encoded like the shading output.
struct RgbaImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::byte> pixels; // width * height * 4 bytes
};

// The image with a black strip below it that holds `legend` (paintLegend()), so a figure made from
// it explains its own colours. An empty legend returns the image as it is. The text uses the
// TrueType font `fontFile`; an empty path takes the font built into Dear ImGui.
[[nodiscard]] RgbaImage withLegend(const RgbaImage& image, const ViewLegend& legend,
                                   const std::filesystem::path& fontFile = {});

} // namespace thesis::resolve
