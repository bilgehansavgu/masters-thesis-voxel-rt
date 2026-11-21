#pragma once

// Side: CPU

#include "thesis/resolve/ColorScale.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace thesis::resolve {

// An image in memory: RGBA8, rows from the top, display-encoded like the shading output.
struct RgbaImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::byte> pixels; // width * height * 4 bytes
};

// The image with a strip below it that holds the colour bar of `scale`: the title, a black box
// "0", the map with its ticks, and a white box for counts above the maximum. A figure made from
// the result explains its own colours. The text uses the TrueType font `fontFile`; an empty path
// takes the font built into Dear ImGui.
[[nodiscard]] RgbaImage withColorBar(const RgbaImage& image, const ColorScale& scale, const std::string& title,
                                     const std::filesystem::path& fontFile = {});

} // namespace thesis::resolve
