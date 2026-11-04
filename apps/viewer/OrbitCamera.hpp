#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace woodyn::viewer {

/// Orbit camera around a target point (Y up).
class OrbitCamera {
public:
    glm::vec3 target{0.0f, 0.04f, -0.04f};
    float yaw = 0.6f;    // radians around +Y
    float pitch = 0.45f; // radians above the horizon
    float distance = 0.75f;
    float fovY = 0.75f;  // radians
    float nearPlane = 0.005f;
    float farPlane = 50.0f;

    void orbit(float dx, float dy);
    void zoom(float wheelSteps);
    /// Moves the target in the camera's screen plane (pixels scaled by distance).
    void pan(float dx, float dy);

    [[nodiscard]] glm::vec3 eye() const;
    [[nodiscard]] glm::mat4 view() const;
    [[nodiscard]] glm::mat4 projection(float aspect) const;
};

} // namespace woodyn::viewer
