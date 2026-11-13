#include "thesis/core/Camera.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>

namespace thesis::core {

CameraFrame OrbitCamera::frame(float aspectRatio) const
{
    const float pitch = glm::radians(std::clamp(pitchDegrees, -89.0f, 89.0f));
    const float yaw = glm::radians(yawDegrees);
    const glm::vec3 offset{std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw)};

    CameraFrame result;
    result.origin = target + distance * offset;
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
