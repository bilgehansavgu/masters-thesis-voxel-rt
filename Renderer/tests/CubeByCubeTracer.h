#pragma once

#include "thesis/core/GpuTypes.h"
#include "thesis/voxel/VoxelModel.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace thesis::tests {

// The answer to "what does this ray hit in this voxel model", found in the plainest way: the
// ray is tested against the cube of every filled voxel, one after the other, in double
// precision. It uses no encoding, no brick walk and no search structure, so it shares no code
// and no idea with the tracers. That makes it the check for bugs that all tracers have in
// common, because all tracers run the same brick test.
//
// It is slow (rays x voxels) and exists only for tests on small models.
class CubeByCubeTracer {
public:
    explicit CubeByCubeTracer(const voxel::VoxelModel& model);

    // The hit list a correct tracer returns for `rays` on any encoding of the model:
    // t, the primitive (the index of the brick in the model), the face of the voxel, and the
    // stored normal in the attributes.
    [[nodiscard]] std::vector<core::Hit> trace(std::span<const core::Ray> rays) const;

private:
    struct Cube {
        glm::dvec3 lo;
        std::uint32_t brick;
        std::uint16_t normal;
    };

    [[nodiscard]] core::Hit trace(const core::Ray& ray) const;

    double voxelSize_ = 1.0;
    std::vector<Cube> cubes_;
};

} // namespace thesis::tests
