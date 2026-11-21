#include "thesis/resolve/ColorBarImage.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <stdexcept>

namespace thesis::resolve {
namespace {

constexpr DisplayColor kStripColor{0.0f, 0.0f, 0.0f}; // the background of work views: black
constexpr DisplayColor kTextColor{0.92f, 0.92f, 0.92f};
constexpr DisplayColor kOutlineColor{0.6f, 0.6f, 0.6f};

int rounded(float value)
{
    return static_cast<int>(std::lround(value));
}

// The pixels of an RGBA8 image, with clipping at its edges.
class Canvas {
public:
    explicit Canvas(RgbaImage& image) : image_(image) {}

    void fill(int x0, int y0, int x1, int y1, DisplayColor color)
    {
        for (int y = std::max(y0, 0); y < std::min(y1, height()); ++y) {
            for (int x = std::max(x0, 0); x < std::min(x1, width()); ++x) {
                blend(x, y, color, 1.0f);
            }
        }
    }

    void outline(int x0, int y0, int x1, int y1, DisplayColor color)
    {
        fill(x0 - 1, y0 - 1, x1 + 1, y0, color);
        fill(x0 - 1, y1, x1 + 1, y1 + 1, color);
        fill(x0 - 1, y0, x0, y1, color);
        fill(x1, y0, x1 + 1, y1, color);
    }

    void blend(int x, int y, DisplayColor color, float alpha)
    {
        if (x < 0 || y < 0 || x >= width() || y >= height()) {
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

private:
    [[nodiscard]] int width() const { return static_cast<int>(image_.width); }
    [[nodiscard]] int height() const { return static_cast<int>(image_.height); }

    RgbaImage& image_;
};

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
                throw std::invalid_argument("Colour bar: font file '" + file.string() + "' does not exist");
            }
            font_ = atlas_.AddFontFromFileTTF(file.string().c_str(), pixels, &config);
        }
        if (font_ == nullptr || !atlas_.Build()) {
            throw std::runtime_error("Colour bar: cannot build the font '" + file.string() + "'");
        }
        atlas_.GetTexDataAsAlpha8(&alpha_, &atlasWidth_, &atlasHeight_);
    }

    [[nodiscard]] float size() const { return font_->FontSize; }

    [[nodiscard]] float width(const std::string& text) const
    {
        return font_->CalcTextSizeA(font_->FontSize, FLT_MAX, 0.0f, text.c_str()).x;
    }

    // Draws `text` with its top left corner at (x, y).
    void draw(Canvas& canvas, float x, float y, const std::string& text, DisplayColor color) const
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
                            canvas.blend(left + column, top + row, color, static_cast<float>(coverage) / 255.0f);
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

} // namespace

RgbaImage withColorBar(const RgbaImage& image, const ColorScale& scale, const std::string& title,
                       const std::filesystem::path& fontFile)
{
    if (image.pixels.size() != static_cast<std::size_t>(image.width) * image.height * 4) {
        throw std::invalid_argument("Colour bar: the pixels do not match the size of the image");
    }
    // Every length follows the text size, and the text size follows the image width, so the bar
    // keeps its proportions at every resolution.
    const Font font(fontFile, std::clamp(static_cast<float>(image.width) / 64.0f, 12.0f, 48.0f));
    const float s = font.size();
    const float pad = std::round(0.6f * s);
    const float barHeight = std::round(1.2f * s);
    const float tickLength = std::round(0.3f * s);
    const float gap = std::round(0.3f * s);
    const std::string zeroLabel = "0";
    const std::string aboveLabel = scale.aboveLabel();
    const float boxWidth = std::round(std::max(1.6f * s, font.width(aboveLabel) + 0.8f * s));

    const float barWidth = std::round(0.6f * static_cast<float>(image.width));
    const float barLeft = std::round(0.5f * (static_cast<float>(image.width) - barWidth));
    const float rampLeft = barLeft + boxWidth + gap;
    const float rampRight = barLeft + barWidth - boxWidth - gap;
    if (rampRight - rampLeft < 2.0f * s) {
        throw std::invalid_argument("Colour bar: the image is too narrow for the bar");
    }

    const float stripHeight = pad + s + 0.5f * pad + barHeight + tickLength + s + pad;
    RgbaImage result;
    result.width = image.width;
    result.height = image.height + static_cast<std::uint32_t>(stripHeight);
    result.pixels.resize(static_cast<std::size_t>(result.width) * result.height * 4);
    std::copy(image.pixels.begin(), image.pixels.end(), result.pixels.begin());

    Canvas canvas(result);
    const float stripTop = static_cast<float>(image.height);
    canvas.fill(0, rounded(stripTop), static_cast<int>(result.width), static_cast<int>(result.height), kStripColor);

    const std::string fullTitle = title + " (" + scale.kindName() + " scale)";
    font.draw(canvas, std::round(0.5f * (static_cast<float>(image.width) - font.width(fullTitle))), stripTop + pad,
              fullTitle, kTextColor);

    const int barTop = rounded(stripTop + pad + s + 0.5f * pad);
    const int barBottom = barTop + rounded(barHeight);
    const float labelTop = static_cast<float>(barBottom) + tickLength;
    const float textInBox = static_cast<float>(barTop) + std::round(0.5f * (barHeight - s));

    // The box for 0, black, and the box for counts above the maximum, white, each with its label.
    const int zeroLeft = rounded(barLeft);
    const int zeroRight = rounded(barLeft + boxWidth);
    canvas.fill(zeroLeft, barTop, zeroRight, barBottom, kZeroCountColor);
    canvas.outline(zeroLeft, barTop, zeroRight, barBottom, kOutlineColor);
    font.draw(canvas, std::round(barLeft + 0.5f * (boxWidth - font.width(zeroLabel))), textInBox, zeroLabel,
              kAboveScaleColor);
    const int aboveLeft = rounded(barLeft + barWidth - boxWidth);
    const int aboveRight = rounded(barLeft + barWidth);
    canvas.fill(aboveLeft, barTop, aboveRight, barBottom, kAboveScaleColor);
    canvas.outline(aboveLeft, barTop, aboveRight, barBottom, kOutlineColor);
    font.draw(canvas, std::round(static_cast<float>(aboveLeft) + 0.5f * (boxWidth - font.width(aboveLabel))), textInBox,
              aboveLabel, kZeroCountColor);

    // The map: each pixel column gets the colour of its position.
    const int left = rounded(rampLeft);
    const int right = rounded(rampRight);
    for (int x = left; x < right; ++x) {
        const float t = (static_cast<float>(x - left) + 0.5f) / static_cast<float>(right - left);
        canvas.fill(x, barTop, x + 1, barBottom, viridis(t));
    }
    canvas.outline(left, barTop, right, barBottom, kOutlineColor);

    // Ticks below the map. A label that would overlap the one before it is left out.
    float previousLabelRight = -FLT_MAX;
    for (const ScaleTick& tick : scale.ticks()) {
        const int x = rounded(rampLeft + tick.position * (rampRight - rampLeft));
        canvas.fill(x, barBottom, x + 1, barBottom + rounded(tickLength), kTextColor);
        const float labelWidth = font.width(tick.label);
        const float labelLeft = std::clamp(static_cast<float>(x) - 0.5f * labelWidth, rampLeft, rampRight - labelWidth);
        if (labelLeft < previousLabelRight + 0.5f * s) {
            continue;
        }
        font.draw(canvas, std::round(labelLeft), labelTop, tick.label, kTextColor);
        previousLabelRight = labelLeft + labelWidth;
    }
    return result;
}

} // namespace thesis::resolve
