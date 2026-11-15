#include "thesis/voxel/Voxelizer.h"

#include "thesis/voxel/OctNormal.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <execution>
#include <numeric>
#include <stdexcept>
#include <string>

namespace thesis::voxel {
namespace {

using Clock = std::chrono::steady_clock;

double secondsSince(Clock::time_point start)
{
    return std::chrono::duration<double>(Clock::now() - start).count();
}

// A triangle in voxel units: voxel (x, y, z) is the cube [x, x+1] x [y, y+1] x [z, z+1].
struct Triangle {
    glm::dvec3 a;
    glm::dvec3 b;
    glm::dvec3 c;
};

// Working memory of one thread for one brick.
struct Scratch {
    std::vector<std::uint64_t> occupancy;
    std::vector<glm::vec3> normalSum; // per voxel; all zero between bricks
};

// Twice the signed area of the 2D triangle (p, q, s). The two end points are put into a fixed
// order first, so edge(p, q, s) == -edge(q, p, s) holds exactly, rounding included. Two
// triangles that share an edge can then never both reject a sample next to that edge, which
// would leave a hole.
double edge(const glm::dvec2& p, const glm::dvec2& q, const glm::dvec2& s)
{
    const bool swapped = q.x < p.x || (q.x == p.x && q.y < p.y);
    const glm::dvec2& first = swapped ? q : p;
    const glm::dvec2& second = swapped ? p : q;
    const double area = (second.x - first.x) * (s.y - first.y) - (second.y - first.y) * (s.x - first.x);
    return swapped ? -area : area;
}

void fill(Scratch& scratch, std::uint32_t brickSize, const glm::ivec3& voxel, const glm::vec3& weightedNormal)
{
    const std::uint32_t index = voxelIndex(brickSize, static_cast<std::uint32_t>(voxel.x),
                                           static_cast<std::uint32_t>(voxel.y), static_cast<std::uint32_t>(voxel.z));
    setFilled(scratch.occupancy, index);
    scratch.normalSum[index] += weightedNormal;
}

// VoxelizationRule::Sampled, for the voxels [lo, lo + n) of one brick.
void fillSampled(const Triangle& t, const glm::vec3& normal, const glm::ivec3& lo, int n, Scratch& scratch)
{
    for (int axis = 0; axis < 3; ++axis) {
        const int u = (axis + 1) % 3;
        const int v = (axis + 2) % 3;
        const glm::dvec2 a(t.a[u], t.a[v]);
        const glm::dvec2 b(t.b[u], t.b[v]);
        const glm::dvec2 c(t.c[u], t.c[v]);
        if (edge(a, b, c) == 0.0) {
            continue; // seen edge-on along this axis: no line crosses it
        }

        // Columns whose centre line (i + 0.5, j + 0.5) can cross the triangle, inside this brick.
        const int uFirst = std::max(lo[u], static_cast<int>(std::ceil(std::min({a.x, b.x, c.x}) - 0.5)));
        const int uLast = std::min(lo[u] + n - 1, static_cast<int>(std::floor(std::max({a.x, b.x, c.x}) - 0.5)));
        const int vFirst = std::max(lo[v], static_cast<int>(std::ceil(std::min({a.y, b.y, c.y}) - 0.5)));
        const int vLast = std::min(lo[v] + n - 1, static_cast<int>(std::floor(std::max({a.y, b.y, c.y}) - 0.5)));
        const double depthMin = std::min({t.a[axis], t.b[axis], t.c[axis]});
        const double depthMax = std::max({t.a[axis], t.b[axis], t.c[axis]});

        for (int j = vFirst; j <= vLast; ++j) {
            for (int i = uFirst; i <= uLast; ++i) {
                const glm::dvec2 sample(i + 0.5, j + 0.5);
                const double wa = edge(b, c, sample);
                const double wb = edge(c, a, sample);
                const double wc = edge(a, b, sample);
                const bool inside = (wa >= 0.0 && wb >= 0.0 && wc >= 0.0) || (wa <= 0.0 && wb <= 0.0 && wc <= 0.0);
                const double sum = wa + wb + wc;
                if (!inside || sum == 0.0) {
                    continue;
                }
                // Where the line crosses the triangle. Clamped to the triangle so that rounding
                // cannot move the voxel into a brick that this triangle was not sorted into.
                const double depth =
                    std::clamp((wa * t.a[axis] + wb * t.b[axis] + wc * t.c[axis]) / sum, depthMin, depthMax);
                const int k = static_cast<int>(std::floor(depth));
                if (k < lo[axis] || k >= lo[axis] + n) {
                    continue; // that voxel belongs to another brick
                }
                glm::ivec3 voxel;
                voxel[axis] = k - lo[axis];
                voxel[u] = i - lo[u];
                voxel[v] = j - lo[v];
                fill(scratch, static_cast<std::uint32_t>(n), voxel, normal);
            }
        }
    }
}

// True when the triangle touches the cube with the given centre and half side (separating axis test).
bool triangleTouchesCube(const glm::dvec3& centre, double half, const Triangle& t)
{
    const glm::dvec3 corners[3] = {t.a - centre, t.b - centre, t.c - centre};
    const glm::dvec3 edges[3] = {corners[1] - corners[0], corners[2] - corners[1], corners[0] - corners[2]};

    const auto separates = [&](const glm::dvec3& direction) {
        const double p0 = glm::dot(direction, corners[0]);
        const double p1 = glm::dot(direction, corners[1]);
        const double p2 = glm::dot(direction, corners[2]);
        const double radius = half * (std::abs(direction.x) + std::abs(direction.y) + std::abs(direction.z));
        return std::min({p0, p1, p2}) > radius || std::max({p0, p1, p2}) < -radius;
    };

    const glm::dvec3 axes[3] = {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};
    for (const glm::dvec3& axis : axes) {
        if (separates(axis)) {
            return false;
        }
    }
    if (separates(glm::cross(edges[0], edges[1]))) {
        return false;
    }
    for (const glm::dvec3& axis : axes) {
        for (const glm::dvec3& side : edges) {
            if (separates(glm::cross(axis, side))) {
                return false;
            }
        }
    }
    return true;
}

// VoxelizationRule::Conservative, for the voxels [lo, lo + n) of one brick.
void fillConservative(const Triangle& t, const glm::vec3& weightedNormal, const glm::ivec3& lo, int n, Scratch& scratch)
{
    const glm::ivec3 first = glm::max(lo, glm::ivec3(glm::floor(glm::min(t.a, glm::min(t.b, t.c)))));
    const glm::ivec3 last = glm::min(lo + (n - 1), glm::ivec3(glm::floor(glm::max(t.a, glm::max(t.b, t.c)))));
    for (int z = first.z; z <= last.z; ++z) {
        for (int y = first.y; y <= last.y; ++y) {
            for (int x = first.x; x <= last.x; ++x) {
                if (triangleTouchesCube({x + 0.5, y + 0.5, z + 0.5}, 0.5, t)) {
                    fill(scratch, static_cast<std::uint32_t>(n), glm::ivec3(x, y, z) - lo, weightedNormal);
                }
            }
        }
    }
}

// The triangles that can fill voxels of one brick: a range of the sorted (brick, triangle) list.
struct BrickJob {
    std::uint32_t brick = 0; // x + bricksX * (y + bricksY * z)
    std::size_t begin = 0;
    std::size_t end = 0;
};

} // namespace

std::string_view ruleName(VoxelizationRule rule)
{
    return rule == VoxelizationRule::Sampled ? "sampled" : "conservative";
}

VoxelizationRule ruleFromName(std::string_view name)
{
    if (name == "sampled") {
        return VoxelizationRule::Sampled;
    }
    if (name == "conservative") {
        return VoxelizationRule::Conservative;
    }
    throw std::invalid_argument("Unknown voxelization rule '" + std::string(name) + "' (sampled or conservative)");
}

VoxelModel voxelize(const mesh::TriangleMesh& mesh, const VoxelizeOptions& options, VoxelizeReport* report)
{
    const std::uint32_t n = options.brickSize;
    if (!isSupportedBrickSize(n)) {
        throw std::invalid_argument("voxelize: brick size must be 8, 16 or 32");
    }
    if (options.resolution < n || options.resolution % n != 0) {
        throw std::invalid_argument("voxelize: resolution must be a positive multiple of the brick size");
    }
    if (mesh.triangleCount() == 0) {
        throw std::invalid_argument("voxelize: mesh '" + mesh.name + "' has no triangles");
    }

    // The grid: the longest side of the mesh spans `resolution` voxels, half a voxel of margin
    // on each side. The stored float values are used from here on, so that a later reader of
    // the model computes the same positions.
    glm::dvec3 meshLo(mesh.positions.front());
    glm::dvec3 meshHi(mesh.positions.front());
    for (const glm::vec3& position : mesh.positions) {
        meshLo = glm::min(meshLo, glm::dvec3(position));
        meshHi = glm::max(meshHi, glm::dvec3(position));
    }
    const glm::dvec3 extent = meshHi - meshLo;
    const double longest = std::max({extent.x, extent.y, extent.z});
    if (!(longest > 0.0)) {
        throw std::invalid_argument("voxelize: mesh '" + mesh.name + "' has no size");
    }

    VoxelModel model;
    model.name = mesh.name + "-" + std::to_string(options.resolution);
    model.source = {mesh.name, mesh.triangleCount(), std::string(ruleName(options.rule)), options.resolution};
    model.grid.brickSize = n;
    model.grid.voxelSize = static_cast<float>(longest / (options.resolution - 1));
    const double voxelSize = model.grid.voxelSize;
    model.grid.origin = glm::vec3(meshLo - 0.5 * voxelSize);
    const glm::dvec3 origin(model.grid.origin);
    for (int axis = 0; axis < 3; ++axis) {
        const auto used = static_cast<std::uint32_t>(std::floor((meshHi[axis] - origin[axis]) / voxelSize)) + 1;
        model.grid.size[axis] = (used + n - 1) / n * n;
    }
    const glm::uvec3 bricksPerAxis = model.grid.size / n;
    if (static_cast<std::uint64_t>(bricksPerAxis.x) * bricksPerAxis.y * bricksPerAxis.z > 0xFFFFFFFFull) {
        throw std::invalid_argument("voxelize: the brick grid is too large");
    }

    const auto toVoxelUnits = [&](std::uint32_t vertex) {
        return (glm::dvec3(mesh.positions[vertex]) - origin) / voxelSize;
    };
    const auto loadTriangle = [&](std::size_t triangle) {
        return Triangle{toVoxelUnits(mesh.indices[3 * triangle]), toVoxelUnits(mesh.indices[3 * triangle + 1]),
                        toVoxelUnits(mesh.indices[3 * triangle + 2])};
    };

    // 1. Sort the triangles into bricks: one (brick, triangle) entry for every brick that the
    //    bounding box of a triangle overlaps. Sorting the entries groups them by brick.
    const Clock::time_point binStart = Clock::now();
    std::uint64_t degenerate = 0;
    std::vector<std::uint64_t> entries;
    entries.reserve(mesh.triangleCount() + mesh.triangleCount() / 4);
    const glm::dvec3 lastVoxel(model.grid.size - 1u);
    for (std::size_t triangle = 0; triangle < mesh.triangleCount(); ++triangle) {
        const Triangle t = loadTriangle(triangle);
        const glm::dvec3 areaNormal = glm::cross(t.b - t.a, t.c - t.a);
        if (glm::dot(areaNormal, areaNormal) == 0.0) {
            ++degenerate;
            continue;
        }
        const glm::uvec3 first(glm::clamp(glm::floor(glm::min(t.a, glm::min(t.b, t.c))), glm::dvec3(0.0), lastVoxel));
        const glm::uvec3 last(glm::clamp(glm::floor(glm::max(t.a, glm::max(t.b, t.c))), glm::dvec3(0.0), lastVoxel));
        for (std::uint32_t z = first.z / n; z <= last.z / n; ++z) {
            for (std::uint32_t y = first.y / n; y <= last.y / n; ++y) {
                for (std::uint32_t x = first.x / n; x <= last.x / n; ++x) {
                    const std::uint64_t brick = x + bricksPerAxis.x * (y + bricksPerAxis.y * z);
                    entries.push_back((brick << 32) | triangle);
                }
            }
        }
    }
    std::sort(std::execution::par_unseq, entries.begin(), entries.end());

    std::vector<BrickJob> jobs;
    for (std::size_t i = 0; i < entries.size();) {
        const auto brick = static_cast<std::uint32_t>(entries[i] >> 32);
        std::size_t end = i;
        while (end < entries.size() && static_cast<std::uint32_t>(entries[end] >> 32) == brick) {
            ++end;
        }
        jobs.push_back({brick, i, end});
        i = end;
    }
    const double binSeconds = secondsSince(binStart);

    // 2. Fill each brick from its own triangles. Bricks do not share voxels, so they run in
    //    parallel; each result goes to its own slot, so the order of the output is fixed.
    const Clock::time_point fillStart = Clock::now();
    const std::size_t words = voxelsPerBrick(n) / 64;
    std::vector<VoxelBrick> filled(jobs.size());
    std::vector<std::size_t> order(jobs.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::for_each(std::execution::par, order.begin(), order.end(), [&](std::size_t jobIndex) {
        thread_local Scratch scratch;
        scratch.occupancy.assign(words, 0);
        if (scratch.normalSum.size() != voxelsPerBrick(n)) {
            scratch.normalSum.assign(voxelsPerBrick(n), glm::vec3(0.0f));
        }

        const BrickJob& job = jobs[jobIndex];
        const glm::uvec3 coordinate(job.brick % bricksPerAxis.x, (job.brick / bricksPerAxis.x) % bricksPerAxis.y,
                                    job.brick / (bricksPerAxis.x * bricksPerAxis.y));
        const glm::ivec3 lo(coordinate * n);
        for (std::size_t entry = job.begin; entry < job.end; ++entry) {
            const Triangle t = loadTriangle(static_cast<std::size_t>(entries[entry] & 0xFFFFFFFFull));
            const glm::dvec3 areaNormal = glm::cross(t.b - t.a, t.c - t.a);
            if (options.rule == VoxelizationRule::Sampled) {
                fillSampled(t, glm::vec3(glm::normalize(areaNormal)), lo, static_cast<int>(n), scratch);
            } else {
                fillConservative(t, glm::vec3(areaNormal), lo, static_cast<int>(n), scratch);
            }
        }

        const std::uint32_t count = filledCount(scratch.occupancy);
        if (count == 0) {
            return; // the triangles came near this brick but filled none of its voxels
        }
        VoxelBrick& brick = filled[jobIndex];
        brick.coordinate = coordinate;
        brick.occupancy = scratch.occupancy;
        brick.normals.reserve(count);
        for (std::uint32_t index = 0; index < voxelsPerBrick(n); ++index) {
            if (isFilled(scratch.occupancy, index)) {
                glm::vec3& sum = scratch.normalSum[index];
                const float length = glm::length(sum);
                brick.normals.push_back(encodeNormal(length > 0.0f ? sum / length : glm::vec3(0.0f, 0.0f, 1.0f)));
                sum = glm::vec3(0.0f);
            }
        }
    });

    for (VoxelBrick& brick : filled) {
        if (!brick.normals.empty()) {
            model.bricks.push_back(std::move(brick));
        }
    }

    if (report != nullptr) {
        report->triangles = mesh.triangleCount();
        report->degenerateTriangles = degenerate;
        report->bricks = model.bricks.size();
        report->voxels = model.voxelCount();
        report->binSeconds = binSeconds;
        report->voxelizeSeconds = secondsSince(fillStart);
    }
    return model;
}

} // namespace thesis::voxel
