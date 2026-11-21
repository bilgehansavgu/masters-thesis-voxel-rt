#include "Overlay.h"

#include "thesis/shared/RayOrder.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace thesis::app {

namespace {

// A line behind the surface by more than this fraction of the surface distance counts as hidden.
// Brick boxes sit right on the surface; the margin keeps their edges from flickering.
constexpr float kDepthMargin = 0.01f;

ImU32 faint(ImU32 colour)
{
    const auto alpha = static_cast<ImU32>(((colour >> IM_COL32_A_SHIFT) & 0xFF) * 0.22f);
    return (colour & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
}

} // namespace

DepthMap DepthMap::fromHits(std::span<const core::Hit> hits, std::uint32_t width, std::uint32_t height,
                            std::uint32_t rayOrder)
{
    DepthMap map;
    map.width = width;
    map.height = height;
    map.distance.reserve(hits.size());
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const core::Hit& hit = hits[THESIS_RAY_INDEX(rayOrder, x, y, width, height)];
            map.distance.push_back(hit.primitive == core::kNoHit ? std::numeric_limits<float>::infinity() : hit.t);
        }
    }
    return map;
}

OverlayPainter::OverlayPainter(ImDrawList* draw, const core::CameraFrame& camera, float left, float top, float width,
                               float height, const DepthMap* depth, bool orthographic)
    : draw_(draw),
      camera_(camera),
      left_(left),
      top_(top),
      width_(width),
      height_(height),
      depth_(depth && !depth->distance.empty() ? depth : nullptr),
      orthographic_(orthographic)
{}

glm::vec3 OverlayPainter::cameraSpace(glm::vec3 world) const
{
    const glm::vec3 delta = world - camera_.origin;
    return {glm::dot(delta, camera_.right) / glm::dot(camera_.right, camera_.right),
            glm::dot(delta, camera_.up) / glm::dot(camera_.up, camera_.up),
            orthographic_ ? 1.0f : glm::dot(delta, camera_.forward)};
}

ImVec2 OverlayPainter::project(glm::vec3 p) const
{
    return {left_ + width_ * (0.5f + 0.5f * p.x / p.z), top_ + height_ * (0.5f - 0.5f * p.y / p.z)};
}

bool OverlayPainter::hidden(glm::vec3 world) const
{
    if (!depth_) {
        return false;
    }
    const glm::vec3 p = cameraSpace(world);
    if (p.z <= 0.0f) {
        return false;
    }
    const ImVec2 screen = project(p);
    const auto x = static_cast<int>((screen.x - left_) / width_ * static_cast<float>(depth_->width));
    const auto y = static_cast<int>((screen.y - top_) / height_ * static_cast<float>(depth_->height));
    if (x < 0 || y < 0 || x >= static_cast<int>(depth_->width) || y >= static_cast<int>(depth_->height)) {
        return false;
    }
    const float surface = depth_->distance[static_cast<std::size_t>(y) * depth_->width + static_cast<std::size_t>(x)];
    return glm::length(world - camera_.origin) > surface * (1.0f + kDepthMargin);
}

void OverlayPainter::line(glm::vec3 start, glm::vec3 end, ImU32 colour, float thickness) const
{
    // Clip in camera space before the division by distance (Liang-Barsky on the view pyramid).
    const glm::vec3 a = cameraSpace(start), b = cameraSpace(end);
    float t0 = 0.0f, t1 = 1.0f;
    for (const glm::vec3 plane : {glm::vec3{1, 0, 1}, {-1, 0, 1}, {0, 1, 1}, {0, -1, 1}, {0, 0, 1}}) {
        const float inset = plane == glm::vec3{0, 0, 1} ? 0.0001f : 0.0f;
        const float d0 = glm::dot(plane, a) - inset, d1 = glm::dot(plane, b) - inset;
        if (d0 < 0 && d1 < 0) {
            return;
        }
        if ((d0 < 0) != (d1 < 0)) {
            const float cut = d0 / (d0 - d1);
            if (d0 < 0) {
                t0 = std::max(t0, cut);
            } else {
                t1 = std::min(t1, cut);
            }
        }
    }
    if (t0 >= t1) {
        return;
    }
    const glm::vec3 worldA = start + (end - start) * t0, worldB = start + (end - start) * t1;
    segment(worldA, worldB, a + (b - a) * t0, a + (b - a) * t1, colour, thickness);
}

void OverlayPainter::segment(glm::vec3 worldA, glm::vec3 worldB, glm::vec3 a, glm::vec3 b, ImU32 colour,
                             float thickness) const
{
    if (!depth_) {
        draw_->AddLine(project(a), project(b), colour, thickness);
        return;
    }
    // Split the line into pieces of about 6 pixels and test the middle of each against the depth.
    const ImVec2 pa = project(a), pb = project(b);
    const float pixels = std::hypot(pb.x - pa.x, pb.y - pa.y);
    const int pieces = std::clamp(static_cast<int>(pixels / 6.0f), 1, 48);
    int runStart = 0;
    bool runHidden = hidden(worldA + (worldB - worldA) * (0.5f / static_cast<float>(pieces)));
    const auto flush = [&](int runEnd) {
        const float s = static_cast<float>(runStart) / static_cast<float>(pieces);
        const float e = static_cast<float>(runEnd) / static_cast<float>(pieces);
        draw_->AddLine(project(a + (b - a) * s), project(a + (b - a) * e), runHidden ? faint(colour) : colour,
                       runHidden ? std::max(1.0f, thickness * 0.6f) : thickness);
    };
    for (int i = 1; i < pieces; ++i) {
        const bool pieceHidden =
            hidden(worldA + (worldB - worldA) * ((static_cast<float>(i) + 0.5f) / static_cast<float>(pieces)));
        if (pieceHidden != runHidden) {
            flush(i);
            runStart = i;
            runHidden = pieceHidden;
        }
    }
    flush(pieces);
}

void OverlayPainter::box(const core::Aabb& box, ImU32 colour, float thickness) const
{
    std::array<glm::vec3, 8> corners;
    for (int i = 0; i < 8; ++i) {
        corners[i] = {i & 1 ? box.hi.x : box.lo.x, i & 2 ? box.hi.y : box.lo.y, i & 4 ? box.hi.z : box.lo.z};
    }
    for (int i = 0; i < 8; ++i) {
        for (int axis = 0; axis < 3; ++axis) {
            if (!(i & (1 << axis))) {
                line(corners[i], corners[i | (1 << axis)], colour, thickness);
            }
        }
    }
}

void OverlayPainter::flatRect(glm::vec2 lo, glm::vec2 hi, float y, ImU32 colour, float thickness) const
{
    const glm::vec3 a{lo.x, y, lo.y}, b{hi.x, y, lo.y}, c{hi.x, y, hi.y}, d{lo.x, y, hi.y};
    line(a, b, colour, thickness);
    line(b, c, colour, thickness);
    line(c, d, colour, thickness);
    line(d, a, colour, thickness);
}

void OverlayPainter::filledRect(const core::Aabb& box, ImU32 colour) const
{
    const ImVec2 a = project(cameraSpace(box.lo)), b = project(cameraSpace(box.hi));
    draw_->AddRectFilled({std::min(a.x, b.x), std::min(a.y, b.y)}, {std::max(a.x, b.x), std::max(a.y, b.y)}, colour);
}

void OverlayPainter::marker(const core::Aabb& box, ImU32 colour, const char* label, float minSize) const
{
    constexpr float kFar = std::numeric_limits<float>::max();
    ImVec2 lo{kFar, kFar}, hi{-kFar, -kFar};
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 p =
            cameraSpace({i & 1 ? box.hi.x : box.lo.x, i & 2 ? box.hi.y : box.lo.y, i & 4 ? box.hi.z : box.lo.z});
        if (p.z <= 0.0001f)
            return; // partly behind the camera: the box itself is large enough
        const ImVec2 s = project(p);
        lo = {std::min(lo.x, s.x), std::min(lo.y, s.y)};
        hi = {std::max(hi.x, s.x), std::max(hi.y, s.y)};
    }
    const ImVec2 centre{(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f};
    const float half = 0.5f * std::max({hi.x - lo.x, hi.y - lo.y, minSize});
    if (centre.x + half < left_ || centre.y + half < top_ || centre.x - half > left_ + width_ ||
        centre.y - half > top_ + height_)
        return;
    const ImVec2 a{centre.x - half, centre.y - half}, b{centre.x + half, centre.y + half};
    draw_->AddRect(a, b, IM_COL32(18, 25, 32, 220), 2.0f, 0, 4.0f);
    draw_->AddRect(a, b, colour, 2.0f, 0, 2.0f);
    if (label) {
        const ImVec2 size = ImGui::CalcTextSize(label);
        const ImVec2 at{b.x + 6, a.y - size.y - 2};
        draw_->AddLine({b.x, a.y}, {at.x, at.y + size.y}, colour, 1.5f);
        draw_->AddRectFilled({at.x - 3, at.y - 1}, {at.x + size.x + 4, at.y + size.y + 1}, IM_COL32(20, 29, 34, 225),
                             3);
        draw_->AddText(at, colour, label);
    }
}

void OverlayPainter::point(glm::vec3 world, ImU32 colour, const char* label) const
{
    const glm::vec3 p = cameraSpace(world);
    if (p.z <= 0.0001f || std::abs(p.x) > p.z || std::abs(p.y) > p.z) {
        return;
    }
    const ImVec2 at = project(p);
    draw_->AddCircleFilled(at, 5, IM_COL32(18, 25, 32, 255));
    draw_->AddCircleFilled(at, 3, colour);
    if (label) {
        const ImVec2 size = ImGui::CalcTextSize(label);
        draw_->AddRectFilled({at.x + 7, at.y - size.y}, {at.x + 15 + size.x, at.y + 2}, IM_COL32(20, 29, 34, 225), 3);
        draw_->AddText({at.x + 11, at.y - size.y}, IM_COL32_WHITE, label);
    }
}

} // namespace thesis::app
