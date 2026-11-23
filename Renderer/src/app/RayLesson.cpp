#include "RayLesson.h"

#include "thesis/core/RayBox.h"

#include "thesis/shared/Brick2012.h"
#include "thesis/shared/RayLog.h"

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>

namespace thesis::app {

bool BrickVoxels::isFull(int x, int y, int z) const
{
    const int n = static_cast<int>(size);
    if (x < 0 || y < 0 || z < 0 || x >= n || y >= n || z >= n)
        return false;
    const auto bit = static_cast<std::size_t>(x + n * (y + n * z));
    return (bits[bit / 64] >> (bit % 64)) & 1u;
}

PrimaryRay primaryRay(const core::OrbitCamera& camera, glm::uvec2 pixel, glm::uvec2 resolution)
{
    const auto frame = camera.frame(static_cast<float>(resolution.x) / static_cast<float>(resolution.y));
    const glm::vec2 ndc = (glm::vec2(pixel) + 0.5f) / glm::vec2(resolution) * 2.0f - 1.0f;
    return {frame.origin, glm::normalize(frame.forward + ndc.x * frame.right - ndc.y * frame.up)};
}

std::optional<glm::vec2> rayBoxInterval(const PrimaryRay& ray, const core::Aabb& box)
{
    return core::rayBoxInterval(ray.origin, ray.direction, box);
}

namespace {

constexpr const char* kFaceNames[] = {"-x", "+x", "-y", "+y", "-z", "+z"};

// Brick-local voxel coordinates of column (u, v) at `height`: u and v run along the two axes
// after the height axis of the brick.
glm::ivec3 localVoxel(int heightAxis, std::uint32_t u, std::uint32_t v, std::uint32_t height)
{
    glm::ivec3 p{0};
    p[(heightAxis + 1) % 3] = static_cast<int>(u);
    p[(heightAxis + 2) % 3] = static_cast<int>(v);
    p[heightAxis] = static_cast<int>(height);
    return p;
}

} // namespace

RayLesson makeRayLesson(const tracing::RayLog& log, const LessonGeometry& geometry, const core::OrbitCamera& camera,
                        glm::uvec2 pixel, glm::uvec2 resolution, std::optional<RayStage> traversal)
{
    const PrimaryRay ray = primaryRay(camera, pixel, resolution);
    RayLesson lesson;
    lesson.origin = ray.origin;
    lesson.direction = ray.direction;
    lesson.length = camera.distance * 1.5f;
    const bool bricks = geometry.brickSize != 0;
    const auto tested =
        std::count_if(log.steps.begin(), log.steps.end(), [](const auto& e) { return e.kind == THESIS_LOG_PRIMITIVE; });

    RayStage start{"Ray",
                   std::format("pixel ({}, {}) · d = ({:.2f}, {:.2f}, {:.2f})", pixel.x, pixel.y, ray.direction.x,
                               ray.direction.y, ray.direction.z),
                   "Primary ray: the line from the camera through the pixel centre, p(t) = o + t d. t is the "
                   "distance along the ray. Seen from the camera it is a point; Side view (V) shows it as a line.",
                   {{"primary ray"}}};
    lesson.stages.push_back(std::move(start));
    if (traversal)
        lesson.stages.push_back(std::move(*traversal));

    if (log.steps.empty() || log.lostSteps || log.steps.back().kind != THESIS_LOG_RESULT) {
        lesson.stages.push_back({"Result",
                                 "log incomplete",
                                 "The GPU log of this ray did not fit into the log buffer. Pick another pixel.",
                                 {{"log incomplete"}}});
        return lesson;
    }
    const auto& result = log.steps.back();
    if (result.a == core::kNoHit) {
        lesson.stages.push_back({"Result",
                                 std::format("miss · {} {} tested", tested, bricks ? "bricks" : "boxes"),
                                 "No primitive along the ray contains a full voxel. The pixel shows the background.",
                                 {{"miss"}}});
        return lesson;
    }
    lesson.length = result.t;
    lesson.face = result.b;
    const core::Aabb& primitiveBox = geometry.bounds.at(result.a);

    // The visit of the winning primitive: from its PRIMITIVE event to the CLOSER_HIT with the final
    // distance. The same brick can be tested twice; only this visit produced the hit.
    std::size_t visitStart = 0, visitEnd = 0;
    for (std::size_t i = 0, current = 0; i < log.steps.size(); ++i) {
        const auto& e = log.steps[i];
        if (e.kind == THESIS_LOG_PRIMITIVE)
            current = i;
        else if (e.kind == THESIS_LOG_CLOSER_HIT && e.a == result.a && e.t == result.t) {
            visitStart = current;
            visitEnd = i;
        }
    }

    const bool brickData = bricks && geometry.grid && result.a < geometry.bricks.size();
    if (!brickData) {
        RayEvent hit{
            std::format("box {} · face {} · t = {:.4g}", result.a, kFaceNames[std::min(result.b, 5u)], result.t),
            {primitiveBox},
            result.t};
        hit.positive = true;
        lesson.stages.push_back({"Hit", hit.label, "The closest primitive along the ray.", {std::move(hit)}});
        return lesson;
    }

    const auto& grid = *geometry.grid;
    const glm::uvec3 coordinate = geometry.bricks[result.a].coordinate;
    const int axis = geometry.bricks[result.a].columnAxis;
    const glm::vec3 cellLo = grid.origin + glm::vec3(coordinate) * grid.cellSize;
    lesson.brickCell = core::Aabb{cellLo, cellLo + glm::vec3(grid.cellSize)};
    lesson.heightAxis = axis;
    lesson.brickLabel = std::format("brick ({}, {}, {})", coordinate.x, coordinate.y, coordinate.z);
    lesson.voxelSize = grid.cellSize / static_cast<float>(geometry.brickSize);
    if (geometry.voxels)
        lesson.brickVoxels = geometry.voxels(coordinate);
    const std::uint32_t n = geometry.brickSize;
    const std::size_t full = lesson.brickVoxels ? lesson.brickVoxels->full.size() : 0;
    const auto inside = rayBoxInterval(ray, *lesson.brickCell);
    const char* axisName = axis == 0 ? "x" : axis == 1 ? "y" : "z";

    RayStage brick{"Brick",
                   std::format("brick ({}, {}, {}) · {}^3 voxels · {} full ({:.1f} %) · columns along {}", coordinate.x,
                               coordinate.y, coordinate.z, n, full,
                               100.0 * static_cast<double>(full) / (static_cast<double>(n) * n * n), axisName),
                   std::format("Brick: a block of {0} x {0} x {0} voxels, stored together. Brick cell (faint cyan): "
                               "its place in the brick grid. Brick AABB (cyan): the tight box around its full "
                               "voxels; the search structures and the GPU see only this box. The ray tested {1} "
                               "brick AABBs; this brick holds the closest hit.",
                               n, tested),
                   {},
                   true};
    brick.events.push_back(
        {inside ? std::format("ray inside the brick cell for t in [{:.4g}, {:.4g}]", inside->x, inside->y)
                : std::string("brick of the hit"),
         {primitiveBox},
         inside ? std::optional<float>(inside->x) : std::nullopt});
    lesson.stages.push_back(std::move(brick));

    // The brick test of the winning visit, event by event.
    RayStage columns{"Columns", "", "", {}, true, true};
    std::size_t cells = 0, skips = 0;
    for (std::size_t i = visitStart + 1; i < visitEnd; ++i) {
        const auto& e = log.steps[i];
        RayEvent event;
        event.t = e.t;
        switch (e.kind) {
        case THESIS_LOG_MIPMAP: {
            const int side = 1 << e.a;
            event.square = glm::ivec3(static_cast<int>(e.b), static_cast<int>(e.c), side);
            const bool empty = e.d > e.e;
            event.label = empty ? std::format("mipmap L{} · {}x{} columns · empty -> skip", e.a, side, side)
                                : std::format("mipmap L{} · {}x{} columns · heights [{}, {}] -> {}", e.a, side, side,
                                              e.d, e.e, e.f == 1 ? "ray outside, skip" : "descend");
            skips += e.f == 1 ? 1 : 0;
            break;
        }
        case THESIS_LOG_CELL:
            ++cells;
            event.column = glm::ivec2(static_cast<int>(e.a), static_cast<int>(e.b));
            event.label = e.c == THESIS_CELL_EMPTY ? std::format("column ({}, {}) · empty", e.a, e.b)
                          : e.c == THESIS_CELL_HEIGHT
                              ? std::format("column ({}, {}) · one voxel at h = {}", e.a, e.b, e.d)
                              : std::format("column ({}, {}) · runs", e.a, e.b);
            break;
        case THESIS_LOG_RUN:
            event.label = std::format("run h = [{}, {}) -> {}", e.a, e.a + e.b, e.c == 1 ? "ray meets it" : "missed");
            event.positive = e.c == 1;
            break;
        case THESIS_LOG_VOXEL_HIT:
            event.voxel = localVoxel(axis, e.a, e.b, e.c);
            event.column = glm::ivec2(static_cast<int>(e.a), static_cast<int>(e.b));
            event.label =
                std::format("voxel ({}, {}) h = {} · face {} -> hit", e.a, e.b, e.c, kFaceNames[std::min(e.d, 5u)]);
            event.positive = true;
            break;
        default:
            continue;
        }
        // A run or a hit belongs to the column before it: keep that column highlighted.
        if (!event.column && !columns.events.empty())
            event.column = columns.events.back().column;
        columns.events.push_back(std::move(event));
    }
    columns.summary = std::format("{} columns visited · {} mipmap squares skipped", cells, skips);
    columns.tooltip = "Brick test (2012 layout): the brick is a 2D grid of columns along one axis. The walk steps "
                      "through the columns that the ray crosses (2D DDA). A column stores its full voxels as one "
                      "height or as runs (a start height and a length). With a _mip encoding, a min/max mipmap "
                      "stores the lowest and highest full voxel of each square of columns; a square that the ray "
                      "passes above or below is skipped in one step. Drag the close-up to turn it.";
    if (columns.events.empty())
        columns.events.push_back({"no column events recorded"});
    lesson.stages.push_back(std::move(columns));

    // The hit.
    RayEvent hit{std::format("t = {:.4g} · face {}", result.t, kFaceNames[std::min(result.b, 5u)])};
    hit.t = result.t;
    hit.positive = true;
    for (std::size_t i = visitEnd; i-- > visitStart;) {
        if (log.steps[i].kind == THESIS_LOG_VOXEL_HIT) {
            const auto& e = log.steps[i];
            hit.voxel = localVoxel(axis, e.a, e.b, e.c);
            hit.column = glm::ivec2(static_cast<int>(e.a), static_cast<int>(e.b));
            const glm::vec3 lo = cellLo + glm::vec3(*hit.voxel) * lesson.voxelSize;
            hit.boxes.push_back({lo, lo + glm::vec3(lesson.voxelSize)});
            break;
        }
    }
    const std::string hitSummary = hit.label;
    lesson.stages.push_back({"Hit",
                             hitSummary,
                             "The closest full voxel along the ray. The pixel is shaded with the normal stored for "
                             "this voxel.",
                             {std::move(hit)},
                             true});
    return lesson;
}

namespace {

constexpr ImU32 kGold = IM_COL32(255, 205, 66, 255);
constexpr ImU32 kGoldFaint = IM_COL32(255, 205, 66, 90);
constexpr ImU32 kCyan = IM_COL32(76, 232, 230, 255);
constexpr ImU32 kCyanFaint = IM_COL32(76, 232, 230, 110);
constexpr ImU32 kOrange = IM_COL32(255, 140, 70, 255);

bool brickStage(const RayLesson& lesson, std::size_t stage)
{
    return lesson.brickCell && lesson.stages.at(stage).closeUp;
}

} // namespace

void drawRayLesson(const RayLesson& lesson, std::size_t stageIndex, std::size_t eventIndex,
                   const OverlayPainter& painter)
{
    const RayStage& stage = lesson.stages.at(stageIndex);
    const auto end = lesson.origin + lesson.length * lesson.direction;
    painter.line(lesson.origin, end, IM_COL32(32, 30, 24, 220), 5);
    painter.line(lesson.origin, end, kGold, 2);
    painter.point(lesson.origin, kGold, "Camera");
    if (lesson.face)
        painter.point(end, kGold, "Hit");
    if (brickStage(lesson, stageIndex)) {
        painter.box(*lesson.brickCell, kCyanFaint, 1.0f);
        // A brick can be a few pixels wide on screen: frame it so that it stays findable.
        painter.marker(*lesson.brickCell, kCyan, lesson.brickLabel.c_str(), 28.0f);
    }
    if (stage.cumulative) {
        for (std::size_t i = 0; i < eventIndex && i < stage.events.size(); ++i)
            for (const auto& box : stage.events[i].boxes)
                painter.box(box, kGoldFaint, 1.0f);
    }
    if (eventIndex < stage.events.size()) {
        const RayEvent& event = stage.events[eventIndex];
        for (const auto& box : event.boxes)
            painter.box(box, brickStage(lesson, stageIndex) && !event.voxel ? kCyan : kGold, 2.0f);
        if (event.t)
            painter.point(lesson.origin + *event.t * lesson.direction, kGold, nullptr);
    }
}

void drawBrickCloseUp(const RayLesson& lesson, std::size_t stageIndex, std::size_t eventIndex, CloseUpView& view)
{
    if (!lesson.brickCell)
        return;
    const RayStage& stage = lesson.stages.at(stageIndex);
    const int n =
        lesson.brickVoxels
            ? static_cast<int>(lesson.brickVoxels->size)
            : static_cast<int>(std::round((lesson.brickCell->hi.x - lesson.brickCell->lo.x) / lesson.voxelSize));
    const int h = lesson.heightAxis, ua = (h + 1) % 3, va = (h + 2) % 3;

    // Area and mouse: left drag turns, right drag pans, the wheel zooms towards the cursor.
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = std::min(width * 0.8f, ImGui::GetTextLineHeight() * 16.0f);
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::InvisibleButton("brick close-up", {width, height},
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    // The wheel zooms the close-up instead of scrolling the panel.
    ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
    const glm::vec2 middle{start.x + width * 0.5f, start.y + height * 0.5f};
    if (ImGui::IsItemActive() && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)) {
        view.moved = true;
        if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            view.pan += glm::vec2(io.MouseDelta.x, io.MouseDelta.y);
        } else {
            view.yaw -= 0.4f * io.MouseDelta.x;
            view.pitch = std::clamp(view.pitch + 0.4f * io.MouseDelta.y, -89.0f, 89.0f);
        }
    }
    if (ImGui::IsItemHovered()) {
        if (io.MouseWheel != 0.0f) {
            // Keep the point under the cursor in place.
            const float zoom = std::clamp(view.zoom * std::pow(1.2f, io.MouseWheel), 1.0f, 64.0f);
            const glm::vec2 cursor = glm::vec2(io.MousePos.x, io.MousePos.y) - middle;
            view.pan = cursor - (cursor - view.pan) * (zoom / view.zoom);
            view.zoom = zoom;
            view.moved = true;
        }
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            view.zoom = 1.0f;
            view.pan = glm::vec2(0.0f);
        }
    }
    ImGui::SetItemTooltip("Left drag: turn · wheel: zoom · right drag: pan · double-click: fit (zoom %.1fx)",
                          view.zoom);
    const float yaw = view.yaw, pitch = view.pitch;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(start, {start.x + width, start.y + height}, IM_COL32(16, 29, 39, 255), 4.0f);
    draw->PushClipRect(start, {start.x + width, start.y + height}, true);

    // Orthographic view of brick-local space, centred on the brick.
    const float y = glm::radians(yaw), p = glm::radians(pitch);
    const glm::vec3 forward{-std::cos(p) * std::sin(y), -std::sin(p), -std::cos(p) * std::cos(y)};
    const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
    const glm::vec3 up = glm::cross(right, forward);
    const float scale = view.zoom * 0.55f * std::min(width, height) / (static_cast<float>(n) * 0.87f);
    const glm::vec3 centre(static_cast<float>(n) * 0.5f);
    const auto project = [&](glm::vec3 local) {
        const glm::vec3 d = local - centre;
        return ImVec2{middle.x + view.pan.x + glm::dot(d, right) * scale,
                      middle.y + view.pan.y - glm::dot(d, up) * scale};
    };
    const auto line = [&](glm::vec3 a, glm::vec3 b, ImU32 colour, float thickness) {
        draw->AddLine(project(a), project(b), colour, thickness);
    };
    const auto outline = [&](glm::vec3 lo, glm::vec3 hi, ImU32 colour, float thickness) {
        for (int i = 0; i < 8; ++i)
            for (int a = 0; a < 3; ++a)
                if (!(i & (1 << a))) {
                    const int j = i | (1 << a);
                    const glm::vec3 c0{i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z};
                    const glm::vec3 c1{j & 1 ? hi.x : lo.x, j & 2 ? hi.y : lo.y, j & 4 ? hi.z : lo.z};
                    line(c0, c1, colour, thickness);
                }
    };

    // What to highlight: the current event, and for a cumulative stage the columns visited before.
    std::vector<glm::ivec2> visited;
    std::optional<glm::ivec2> column;
    std::optional<glm::ivec3> square, voxel;
    if (eventIndex < stage.events.size()) {
        const RayEvent& event = stage.events[eventIndex];
        column = event.column;
        square = event.square;
        voxel = event.voxel;
        if (stage.cumulative)
            for (std::size_t i = 0; i < eventIndex; ++i)
                if (stage.events[i].column)
                    visited.push_back(*stage.events[i].column);
    }
    const auto inColumn = [&](glm::ivec3 v, glm::ivec2 c) { return v[ua] == c.x && v[va] == c.y; };

    // The brick cell, then the full voxels back to front, each with its visible free faces.
    outline(glm::vec3(0), glm::vec3(static_cast<float>(n)), kCyanFaint, 1.0f);
    if (stage.cumulative) {
        // The 2D grid of columns, on the floor of the brick (height 0): the cells that the DDA walks.
        for (int i = 0; i <= n; ++i) {
            glm::vec3 a(0.0f), b(0.0f);
            a[ua] = b[ua] = static_cast<float>(i);
            b[va] = static_cast<float>(n);
            line(a, b, IM_COL32(110, 140, 160, 70), 1.0f);
            glm::vec3 c(0.0f), d(0.0f);
            c[va] = d[va] = static_cast<float>(i);
            d[ua] = static_cast<float>(n);
            line(c, d, IM_COL32(110, 140, 160, 70), 1.0f);
        }
    }
    if (lesson.brickVoxels) {
        const BrickVoxels& bv = *lesson.brickVoxels;
        std::vector<std::pair<float, std::size_t>> order;
        order.reserve(bv.full.size());
        for (std::size_t i = 0; i < bv.full.size(); ++i)
            order.emplace_back(glm::dot(glm::vec3(bv.full[i]) + 0.5f - centre, forward), i);
        std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        const glm::vec3 light = glm::normalize(-forward + up * 0.6f + right * 0.3f);
        for (const auto& [depth, index] : order) {
            const glm::ivec3 v(bv.full[index]);
            ImU32 base = IM_COL32(150, 160, 170, 255);
            if (voxel && v == *voxel)
                base = IM_COL32(255, 205, 66, 255);
            else if (column && inColumn(v, *column))
                base = IM_COL32(90, 170, 255, 255);
            else if (std::any_of(visited.begin(), visited.end(), [&](glm::ivec2 c) { return inColumn(v, c); }))
                base = IM_COL32(70, 120, 180, 255);
            for (int face = 0; face < 6; ++face) {
                const int a = face / 2;
                const int s = face % 2 ? 1 : -1;
                glm::vec3 normal(0.0f);
                normal[a] = static_cast<float>(s);
                if (glm::dot(normal, forward) >= 0.0f)
                    continue; // faces away from the viewer
                glm::ivec3 next = v;
                next[a] += s;
                if (bv.isFull(next.x, next.y, next.z))
                    continue; // hidden by the neighbour
                const int b = (a + 1) % 3, c = (a + 2) % 3;
                glm::vec3 q[4];
                for (int k = 0; k < 4; ++k) {
                    q[k] = glm::vec3(v);
                    q[k][a] += s > 0 ? 1.0f : 0.0f;
                    q[k][b] += (k == 1 || k == 2) ? 1.0f : 0.0f;
                    q[k][c] += (k >= 2) ? 1.0f : 0.0f;
                }
                const float shade = 0.45f + 0.55f * std::max(0.0f, glm::dot(normal, light));
                const auto channel = [&](int shift) {
                    return static_cast<ImU32>(static_cast<float>((base >> shift) & 0xFF) * shade);
                };
                const ImU32 colour =
                    IM_COL32(channel(IM_COL32_R_SHIFT), channel(IM_COL32_G_SHIFT), channel(IM_COL32_B_SHIFT), 255);
                const ImVec2 points[4] = {project(q[0]), project(q[1]), project(q[2]), project(q[3])};
                draw->AddConvexPolyFilled(points, 4, colour);
                if (scale >= 8.0f) // voxel edges once a voxel is large enough on screen
                    draw->AddPolyline(points, 4, IM_COL32(20, 30, 40, 120), ImDrawFlags_Closed, 1.0f);
            }
        }
    }

    // Column, mipmap square and voxel outlines.
    const auto columnBox = [&](int u, int v, int sideU, int sideV) {
        glm::vec3 lo(0.0f), hi(static_cast<float>(n));
        lo[ua] = static_cast<float>(u);
        hi[ua] = static_cast<float>(u + sideU);
        lo[va] = static_cast<float>(v);
        hi[va] = static_cast<float>(v + sideV);
        return std::pair{lo, hi};
    };
    for (const glm::ivec2 c : visited) {
        const auto [lo, hi] = columnBox(c.x, c.y, 1, 1);
        outline(lo, hi, IM_COL32(90, 150, 230, 150), 1.0f);
    }
    if (square) {
        const auto [lo, hi] = columnBox(square->x, square->y, square->z, square->z);
        outline(lo, hi, kOrange, 2.0f);
    }
    if (column) {
        const auto [lo, hi] = columnBox(column->x, column->y, 1, 1);
        outline(lo, hi, kGold, 2.0f);
    }
    if (voxel)
        outline(glm::vec3(*voxel), glm::vec3(*voxel) + 1.0f, kGold, 2.5f);

    // The ray through the brick, in brick-local voxel units.
    const auto local = [&](glm::vec3 world) { return (world - lesson.brickCell->lo) / lesson.voxelSize; };
    const PrimaryRay ray{lesson.origin, lesson.direction};
    if (const auto inside = rayBoxInterval(ray, *lesson.brickCell)) {
        const float margin = 0.25f * static_cast<float>(n) * lesson.voxelSize;
        const glm::vec3 a = local(lesson.origin + std::max(0.0f, inside->x - margin) * lesson.direction);
        const glm::vec3 b = local(lesson.origin + std::min(inside->y + margin, lesson.length) * lesson.direction);
        line(a, b, IM_COL32(32, 30, 24, 230), 4.0f);
        line(a, b, kGold, 2.0f);
        if (eventIndex < stage.events.size() && stage.events[eventIndex].t) {
            const ImVec2 at = project(local(lesson.origin + *stage.events[eventIndex].t * lesson.direction));
            draw->AddCircleFilled(at, 4.0f, kGold);
        }
    }
    draw->PopClipRect();
}

} // namespace thesis::app
