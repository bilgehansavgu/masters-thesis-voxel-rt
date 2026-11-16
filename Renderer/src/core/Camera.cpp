#include "thesis/core/Camera.h"

// ============================================================================
// READING ORDER: FILE 19D (after Camera.h)
// ============================================================================

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>

namespace thesis::core {

CameraFrame OrbitCamera::frame(float aspectRatio) const
{
    // Clamp pitch away from +-90 degrees so `forward` is never parallel to world-up (0, 1, 0),
    // which would make the cross product below zero ("gimbal lock").
    const float pitch = glm::radians(std::clamp(pitchDegrees, -89.0f, 89.0f));
    const float yaw = glm::radians(yawDegrees);

    // Spherical coordinates -> 3D unit vector pointing from `target` out toward the camera's eye.
    // Y is vertical (`sin(pitch)`), and the horizontal radius `cos(pitch)` is split between
    // X (`sin(yaw)`) and Z (`cos(yaw)`).
    const glm::vec3 offset{std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw)};

    CameraFrame result;
    result.origin = target + distance * offset;

    // `forward` points from the camera eye (`origin`) toward `target`.
    result.forward = glm::normalize(target - result.origin);

    // 3D VECTOR MATH: Cross product (`glm::cross(a, b)`) produces a vector perpendicular to both
    // `a` and `b`.
    // 1. `forward` x `worldUp (0,1,0)` gives a horizontal vector pointing to the camera's right.
    const glm::vec3 right = glm::normalize(glm::cross(result.forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    // 2. `right` x `forward` gives the camera's true `up` vector (tilted when the camera pitches).
    const glm::vec3 up = glm::cross(right, result.forward);

    // At distance 1 in front of the camera, a vertical field-of-view angle `fov` spans a half-height
    // of `tan(fov / 2)` above and below the center line, and `halfHeight * aspectRatio` left and right.
    const float halfHeight = std::tan(0.5f * glm::radians(verticalFovDegrees));
    result.right = right * (halfHeight * aspectRatio);
    result.up = up * halfHeight;
    return result;
}

OrbitCamera OrbitCamera::framing(const Bounds& bounds)
{
    OrbitCamera camera;
    camera.target = bounds.center();
    const float radius = std::max(bounds.radius(), 1.0f);
    const float halfFov = 0.5f * glm::radians(camera.verticalFovDegrees);
    // Trigonometry: to fit a sphere of radius `R` inside a cone of half-angle `halfFov`,
    // the camera must be at distance `R / sin(halfFov)` (multiplied by 1.05 for a 5% margin).
    camera.distance = 1.05f * radius / std::sin(halfFov);
    return camera;
}

} // namespace thesis::core
