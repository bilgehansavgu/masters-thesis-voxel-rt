#pragma once

// Side: CPU

#include "thesis/core/Scene.h"

#include <glm/vec3.hpp>

namespace thesis::core {

// What the ray generator needs: a pinhole camera. `right` and `up` are already scaled so that
// the image plane at distance 1 along `forward` spans [-1, 1] in both directions.
struct CameraFrame {
    glm::vec3 origin{0.0f};
    glm::vec3 forward{0.0f, 0.0f, -1.0f};
    glm::vec3 right{1.0f, 0.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
};

// Camera that circles a target point; used interactively and for scripted benchmark paths.
// It can also fly: turnInPlace() and fly() move the eye and carry the target along, so an orbit
// after a flight circles the point `distance` in front of the eye.
struct OrbitCamera {
    glm::vec3 target{0.0f};
    float distance = 10.0f;
    float yawDegrees = 35.0f;
    float pitchDegrees = 30.0f; // above the horizon
    float verticalFovDegrees = 60.0f;

    [[nodiscard]] CameraFrame frame(float aspectRatio) const;
    [[nodiscard]] glm::vec3 eye() const;

    // Changes yaw and pitch, keeping the eye where it is (looking around).
    void turnInPlace(float yawStepDegrees, float pitchStepDegrees);
    // Moves the eye and the target: `forward` along the view direction, `right` to the side,
    // `up` along the world up axis.
    void fly(float forward, float right, float up);

    // A camera that shows the whole of `bounds`.
    [[nodiscard]] static OrbitCamera framing(const Bounds& bounds);
};

} // namespace thesis::core
