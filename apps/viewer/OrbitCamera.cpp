#include "OrbitCamera.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace woodyn::viewer {

void OrbitCamera::orbit(float dx, float dy) {
    yaw -= dx * 0.006f;
    pitch = std::clamp(pitch + dy * 0.006f, -0.1f, 1.5f);
}

void OrbitCamera::zoom(float wheelSteps) {
    distance = std::clamp(distance * std::pow(0.88f, wheelSteps), 0.08f, 8.0f);
}

void OrbitCamera::pan(float dx, float dy) {
    const glm::vec3 forward = glm::normalize(target - eye());
    const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    const glm::vec3 up = glm::cross(right, forward);
    const float scale = distance * 0.0012f;
    target += (-dx * right + dy * up) * scale;
}

glm::vec3 OrbitCamera::eye() const {
    const float cp = std::cos(pitch);
    return target + distance * glm::vec3(cp * std::sin(yaw), std::sin(pitch), cp * std::cos(yaw));
}

glm::mat4 OrbitCamera::view() const {
    return glm::lookAt(eye(), target, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 OrbitCamera::projection(float aspect) const {
    return glm::perspective(fovY, aspect, nearPlane, farPlane);
}

} // namespace woodyn::viewer
