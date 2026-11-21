#include "thesis/resolve/LegendImage.h"

#include "thesis/resolve/LegendPainter.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <stdexcept>
#include <string>

namespace thesis::resolve {
namespace {

constexpr DisplayColor kStripColor{0.0f, 0.0f, 0.0f};

int rounded(float value)
{
    return static_cast<int>(std::lround(value));
}

DisplayColor mix(DisplayColor a, DisplayColor b, float t)
{
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

// One font size rasterised by Dear ImGui's font atlas (stb_truetype). Needs no ImGui context.
// One atlas pixel per image pixel (no oversampling), so glyphs are copied without resampling.
class Font {
public:
    Font(const std::filesystem::path& file, float pixels)
    {
        ImFontConfig config;
        config.SizePixels = pixels;
        config.OversampleH = 1;
        config.OversampleV = 1;
        config.PixelSnapH = true;
        if (file.empty()) {
            font_ = atlas_.AddFontDefault(&config);
        } else {
            if (!std::filesystem::is_regular_file(file)) {
                throw std::invalid_argument("Legend: font file '" + file.string() + "' does not exist");
            }
            font_ = atlas_.AddFontFromFileTTF(file.string().c_str(), pixels, &config);
        }
        if (font_ == nullptr || !atlas_.Build()) {
            throw std::runtime_error("Legend: cannot build the font '" + file.string() + "'");
        }
        atlas_.GetTexDataAsAlpha8(&alpha_, &atlasWidth_, &atlasHeight_);
    }

    [[nodiscard]] float size() const { return font_->FontSize; }

    [[nodiscard]] float width(const std::string& text) const
    {
        return font_->CalcTextSizeA(font_->FontSize, FLT_MAX, 0.0f, text.c_str()).x;
    }

    // Calls blend(x, y, coverage) for every pixel of `text` with its top left corner at (x, y).
    template <typename Blend>
    void draw(float x, float y, const std::string& text, Blend&& blend) const
    {
        for (const char c : text) {
            const ImFontGlyph* glyph = font_->FindGlyph(static_cast<ImWchar>(static_cast<unsigned char>(c)));
            if (glyph == nullptr) {
                continue;
            }
            if (glyph->Visible) {
                const int left = rounded(x + glyph->X0);
                const int top = rounded(y + glyph->Y0);
                const int u = rounded(glyph->U0 * static_cast<float>(atlasWidth_));
                const int v = rounded(glyph->V0 * static_cast<float>(atlasHeight_));
                const int columns = rounded((glyph->U1 - glyph->U0) * static_cast<float>(atlasWidth_));
                const int rows = rounded((glyph->V1 - glyph->V0) * static_cast<float>(atlasHeight_));
                for (int row = 0; row < rows; ++row) {
                    for (int column = 0; column < columns; ++column) {
                        const unsigned char coverage = alpha_[(v + row) * atlasWidth_ + u + column];
                        if (coverage != 0) {
                            blend(left + column, top + row, static_cast<float>(coverage) / 255.0f);
                        }
                    }
                }
            }
            x += glyph->AdvanceX;
        }
    }

private:
    ImFontAtlas atlas_;
    ImFont* font_ = nullptr;
    unsigned char* alpha_ = nullptr;
    int atlasWidth_ = 0;
    int atlasHeight_ = 0;
};

// Paints into the pixels of an RGBA8 image, clipped at its edges. A pixel belongs to a rectangle
// when its centre is inside it.
class ImagePainter final : public ILegendPainter {
public:
    ImagePainter(RgbaImage& image, const Font& font) : image_(image), font_(font) {}

    [[nodiscard]] float fontSize() const override { return font_.size(); }
    [[nodiscard]] float textWidth(const std::string& text) const override { return font_.width(text); }

    void fillVertical(float x0, float y0, float x1, float y1, DisplayColor top, DisplayColor bottom) override
    {
        const int rowEnd = rounded(y1);
        const int rowStart = rounded(y0);
        for (int y = rowStart; y < rowEnd; ++y) {
            const float t = rowEnd - rowStart > 1
                                ? static_cast<float>(y - rowStart) / static_cast<float>(rowEnd - rowStart - 1)
                                : 0.0f;
            const DisplayColor color = mix(top, bottom, t);
            for (int x = rounded(x0); x < rounded(x1); ++x) {
                blend(x, y, color, 1.0f);
            }
        }
    }

    void fillHorizontal(float x0, float y0, float x1, float y1, DisplayColor left, DisplayColor right) override
    {
        for (int x = rounded(x0); x < rounded(x1); ++x) {
            const float t = std::clamp((static_cast<float>(x) + 0.5f - x0) / std::max(x1 - x0, 1e-6f), 0.0f, 1.0f);
            const DisplayColor color = mix(left, right, t);
            for (int y = rounded(y0); y < rounded(y1); ++y) {
                blend(x, y, color, 1.0f);
            }
        }
    }

    void outline(float x0, float y0, float x1, float y1, DisplayColor color) override
    {
        fillVertical(x0 - 1.0f, y0 - 1.0f, x1 + 1.0f, y0, color, color);
        fillVertical(x0 - 1.0f, y1, x1 + 1.0f, y1 + 1.0f, color, color);
        fillVertical(x0 - 1.0f, y0, x0, y1, color, color);
        fillVertical(x1, y0, x1 + 1.0f, y1, color, color);
    }

    void text(float x, float y, const std::string& text, DisplayColor color) override
    {
        font_.draw(x, y, text, [&](int px, int py, float coverage) { blend(px, py, color, coverage); });
    }

private:
    void blend(int x, int y, DisplayColor color, float alpha)
    {
        if (x < 0 || y < 0 || x >= static_cast<int>(image_.width) || y >= static_cast<int>(image_.height)) {
            return;
        }
        std::byte* pixel =
            &image_.pixels[(static_cast<std::size_t>(y) * image_.width + static_cast<std::size_t>(x)) * 4];
        const float channels[3] = {color.r, color.g, color.b};
        for (std::size_t c = 0; c < 3; ++c) {
            const float old = static_cast<float>(pixel[c]) / 255.0f;
            const float value = old + (std::clamp(channels[c], 0.0f, 1.0f) - old) * alpha;
            pixel[c] = static_cast<std::byte>(std::lround(value * 255.0f));
        }
        pixel[3] = std::byte{255};
    }

    RgbaImage& image_;
    const Font& font_;
};

} // namespace

RgbaImage withLegend(const RgbaImage& image, const ViewLegend& legend, const std::filesystem::path& fontFile)
{
    if (image.pixels.size() != static_cast<std::size_t>(image.width) * image.height * 4) {
        throw std::invalid_argument("Legend: the pixels do not match the size of the image");
    }
    if (legend.empty()) {
        return image;
    }
    // Every length follows the text size, and the text size follows the image width, so the legend
    // keeps its proportions at every resolution.
    const Font font(fontFile, std::clamp(static_cast<float>(image.width) / 64.0f, 12.0f, 48.0f));
    const float pad = std::round(0.6f * font.size());
    const float columnWidth = std::round(0.6f * static_cast<float>(image.width));
    if (columnWidth < 12.0f * font.size()) {
        throw std::invalid_argument("Legend: the image is too narrow for the legend");
    }
    const float columnLeft = std::round(0.5f * (static_cast<float>(image.width) - columnWidth));

    RgbaImage result;
    result.width = image.width;
    result.height = image.height;
    ImagePainter measure(result, font);
    const float stripHeight = std::ceil(pad + legendHeight(measure, legend, columnWidth) + pad);
    result.height = image.height + static_cast<std::uint32_t>(stripHeight);
    result.pixels.resize(static_cast<std::size_t>(result.width) * result.height * 4);
    std::copy(image.pixels.begin(), image.pixels.end(), result.pixels.begin());

    ImagePainter painter(result, font);
    const float stripTop = static_cast<float>(image.height);
    painter.fillVertical(0.0f, stripTop, static_cast<float>(result.width), static_cast<float>(result.height),
                         kStripColor, kStripColor);
    (void)paintLegend(painter, legend, columnLeft, stripTop + pad, columnWidth);
    return result;
}

} // namespace thesis::resolve
