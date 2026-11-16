#pragma once

// ============================================================================
// READING ORDER: FILE 19C (with Camera.cpp as 19D)
// ============================================================================

#include "thesis/core/Scene.h"

#include <glm/vec3.hpp>

namespace thesis::core {

// What the ray generator (`camera_rays.slang`) needs: a pinhole camera basis in 3D world space.
//   - `origin`: where the camera's eye is located.
//   - `forward`: unit vector pointing from `origin` toward the target point.
//   - `right` and `up`: vectors pointing along the screen's horizontal and vertical axes,
//     already scaled by `tan(verticalFov / 2)` (and `aspectRatio` for `right`) so that
//     `forward + ndc.x * right - ndc.y * up` gives the ray direction for any `ndc` in [-1, 1].
struct CameraFrame {
    glm::vec3 origin{0.0f};
    glm::vec3 forward{0.0f, 0.0f, -1.0f};
    glm::vec3 right{1.0f, 0.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
};

// Camera that circles a target point (like rotating a 3D model on a turntable);
// used interactively with the mouse and for scripted 360-degree benchmark paths.
struct OrbitCamera {
    glm::vec3 target{0.0f};           // 3D point the camera looks at and orbits around
    float distance = 10.0f;           // distance from `target` to the camera's eye
    float yawDegrees = 35.0f;         // horizontal rotation angle around the Y (up) axis
    float pitchDegrees = 30.0f;       // vertical elevation angle above the horizon (-89 to +89)
    float verticalFovDegrees = 60.0f; // vertical field-of-view angle of the camera lens

    // Computes the 3D basis vectors (`origin`, `forward`, `right`, `up`) for a given screen aspect ratio
    // (`width / height`).
    [[nodiscard]] CameraFrame frame(float aspectRatio) const;

    // Creates an OrbitCamera positioned far enough back to see all of `bounds`.
    [[nodiscard]] static OrbitCamera framing(const Bounds& bounds);
};

} // namespace thesis::core
