#include "thesis/core/Camera.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>

namespace thesis::core {

namespace {

// Unit vector from the target to the eye.
glm::vec3 eyeDirection(float yawDegrees, float pitchDegrees)
{
    const float pitch = glm::radians(std::clamp(pitchDegrees, -89.0f, 89.0f));
    const float yaw = glm::radians(yawDegrees);
    return {std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw)};
}

} // namespace

glm::vec3 OrbitCamera::eye() const
{
    return target + distance * eyeDirection(yawDegrees, pitchDegrees);
}

void OrbitCamera::turnInPlace(float yawStepDegrees, float pitchStepDegrees)
{
    const glm::vec3 fixedEye = eye();
    yawDegrees += yawStepDegrees;
    pitchDegrees = std::clamp(pitchDegrees + pitchStepDegrees, -89.0f, 89.0f);
    target = fixedEye - distance * eyeDirection(yawDegrees, pitchDegrees);
}

void OrbitCamera::fly(float forward, float right, float up)
{
    const glm::vec3 view = -eyeDirection(yawDegrees, pitchDegrees);
    const glm::vec3 side = glm::normalize(glm::cross(view, glm::vec3(0.0f, 1.0f, 0.0f)));
    target += forward * view + right * side + glm::vec3(0.0f, up, 0.0f);
}

CameraFrame OrbitCamera::frame(float aspectRatio) const
{
    CameraFrame result;
    result.origin = eye();
    result.forward = glm::normalize(target - result.origin);
    const glm::vec3 right = glm::normalize(glm::cross(result.forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    const glm::vec3 up = glm::cross(right, result.forward);

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
    camera.distance = 1.05f * radius / std::sin(halfFov);
    return camera;
}

} // namespace thesis::core
