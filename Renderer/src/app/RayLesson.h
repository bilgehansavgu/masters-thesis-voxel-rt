#pragma once

// Side: View

#include "Overlay.h"

#include "thesis/core/Camera.h"
#include "thesis/encoding/Encoding.h"
#include "thesis/tracing/RayLog.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace thesis::app {

// The full voxels of one brick, in brick-local coordinates (0 .. size-1 on each axis).
struct BrickVoxels {
    std::uint32_t size = 0;          // voxels along one side
    std::vector<glm::u8vec3> full;   // the full voxels
    std::vector<std::uint64_t> bits; // occupancy, bit x + size * (y + size * z), for neighbour tests
    [[nodiscard]] bool isFull(int x, int y, int z) const;
};

// The geometry that the ray walk needs besides the GPU log: where each primitive is, the brick
// grid, and (for a voxel model) a way to read the voxels of one brick.
struct LessonGeometry {
    std::vector<core::Aabb> bounds;
    std::optional<core::RegularGrid> grid;
    std::vector<encoding::BrickPlace> bricks; // the brick of each primitive; empty without bricks
    std::uint32_t brickSize = 0;
    // The voxels of the brick at a brick coordinate, or none.
    std::function<std::optional<BrickVoxels>(glm::uvec3)> voxels;
};

// One recorded event of the ray, as one line of text and what to highlight.
struct RayEvent {
    std::string label;
    std::vector<core::Aabb> boxes; // highlighted on the model (gold)
    std::optional<float> t;        // a point on the ray
    // Highlights in the brick close-up, in brick-local voxel coordinates.
    std::optional<glm::ivec2> column; // (u, v) of a column
    std::optional<glm::ivec3> square; // (u, v, side) of a mipmap square of columns
    std::optional<glm::ivec3> voxel;  // (x, y, z) of the voxel that was hit
    bool positive = false;            // the event found something (a box hit, a brick with a hit, ...)
};

// One stage of the walk: Ray, Traversal, Brick, Columns, Hit. A stage holds one or more events;
// the panel steps through them one at a time.
struct RayStage {
    std::string name;    // short label of the stage button
    std::string summary; // one line of numbers
    std::string tooltip; // what the terms mean
    std::vector<RayEvent> events;
    bool closeUp = false;    // show the close-up of the brick
    bool cumulative = false; // earlier events of the stage stay visible, faint
};

struct RayLesson {
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f};
    float length = 1.0f;
    std::vector<RayStage> stages;

    // The brick that holds the hit, for the close-up.
    std::optional<core::Aabb> brickCell; // the whole brick cell (size^3 voxels) in world space
    std::string brickLabel;              // "brick (x, y, z)", its coordinate in the brick grid
    std::optional<BrickVoxels> brickVoxels;
    int heightAxis = 1; // the axis of the columns of that brick (brick2012 layout)
    float voxelSize = 1.0f;
    std::optional<std::uint32_t> face; // the side of the voxel that was hit: 0..5 = -x, +x, -y, +y, -z, +z
};

// The camera ray through the centre of `pixel`, as the GPU generates it (normalized direction).
struct PrimaryRay {
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f};
};
[[nodiscard]] PrimaryRay primaryRay(const core::OrbitCamera& camera, glm::uvec2 pixel, glm::uvec2 resolution);

// The distances [t0, t1] (t1 >= t0 >= 0) where the ray is inside `box`, or none (slab test).
[[nodiscard]] std::optional<glm::vec2> rayBoxInterval(const PrimaryRay& ray, const core::Aabb& box);

// The ray of `pixel`, its search (`traversal`, from StructureView), and its brick test, from the
// GPU log of that ray.
[[nodiscard]] RayLesson makeRayLesson(const tracing::RayLog& log, const LessonGeometry& geometry,
                                      const core::OrbitCamera& camera, glm::uvec2 pixel, glm::uvec2 resolution,
                                      std::optional<RayStage> traversal);

// Draw the ray and the highlights of one event on the model.
void drawRayLesson(const RayLesson& lesson, std::size_t stage, std::size_t event, const OverlayPainter& painter);

// How the brick close-up is seen. The user changes it with the mouse inside the close-up.
struct CloseUpView {
    float yaw = 0.0f;    // degrees, left drag
    float pitch = 20.0f; // degrees, left drag
    float zoom = 1.0f;   // 1 = the whole brick fits; mouse wheel, towards the cursor
    glm::vec2 pan{0.0f}; // pixels; right drag
    bool moved = false;  // the user turned, zoomed or panned it: a change of stage keeps the view
};

// The close-up of the brick in the panel: its full voxels, the ray, and the highlights of one event.
// Double-click resets zoom and pan.
void drawBrickCloseUp(const RayLesson& lesson, std::size_t stage, std::size_t event, CloseUpView& view);

} // namespace thesis::app
