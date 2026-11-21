#pragma once

// Side: View

#include "thesis/core/Camera.h"
#include "thesis/core/GpuTypes.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <imgui.h>

#include <cstdint>
#include <span>
#include <vector>

namespace thesis::app {

// The distance to the visible surface for each pixel of a rendered frame, from the hit list of
// that frame. A miss has an infinite distance. Valid only for the camera that rendered it.
struct DepthMap {
    std::vector<float> distance;
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    // `rayOrder`: where the hit of each pixel is in `hits` (THESIS_RAY_ORDER_* in thesis/shared/RayOrder.h).
    [[nodiscard]] static DepthMap fromHits(std::span<const core::Hit> hits, std::uint32_t width, std::uint32_t height,
                                           std::uint32_t rayOrder);
};

// Draws 3D lines over the rendered image. With a depth map, the parts of a line that lie behind
// the model are drawn faint, so near and far stay apart. Lines are clipped to the view.
class OverlayPainter {
public:
    // `camera` is the camera of the image; `left`, `top`, `width`, `height` the image on screen.
    // `orthographic`: a flat diagram that ignores distance (used for close-ups in the panel).
    OverlayPainter(ImDrawList* draw, const core::CameraFrame& camera, float left, float top, float width, float height,
                   const DepthMap* depth = nullptr, bool orthographic = false);

    void line(glm::vec3 start, glm::vec3 end, ImU32 colour, float thickness) const;
    void box(const core::Aabb& box, ImU32 colour, float thickness) const;
    // A rectangle in the horizontal plane at height `y`.
    void flatRect(glm::vec2 lo, glm::vec2 hi, float y, ImU32 colour, float thickness) const;
    void filledRect(const core::Aabb& box, ImU32 colour) const; // orthographic diagrams only
    void point(glm::vec3 world, ImU32 colour, const char* label) const;
    // A screen-space frame around `box`, at least `minSize` pixels wide, so that a box of a few
    // pixels stays findable. Not depth tested: it marks where the box is, not what hides it.
    void marker(const core::Aabb& box, ImU32 colour, const char* label, float minSize) const;

private:
    [[nodiscard]] glm::vec3 cameraSpace(glm::vec3 world) const;
    [[nodiscard]] ImVec2 project(glm::vec3 p) const;
    [[nodiscard]] bool hidden(glm::vec3 world) const;
    void segment(glm::vec3 worldA, glm::vec3 worldB, glm::vec3 a, glm::vec3 b, ImU32 colour, float thickness) const;

    ImDrawList* draw_;
    core::CameraFrame camera_;
    float left_, top_, width_, height_;
    const DepthMap* depth_;
    bool orthographic_;
};

} // namespace thesis::app
