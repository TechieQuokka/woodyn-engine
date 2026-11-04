#pragma once

#include "woodyn/core/Units.hpp"
#include "woodyn/simulation/Simulation.hpp"

#include <glm/geometric.hpp>

namespace woodyn::test {

constexpr float u = units::kModule;

/// Runs the simulation for `seconds` of simulated time using fixed steps.
inline void runFor(simulation::Simulation& sim, float seconds) {
    const int steps = static_cast<int>(seconds * sim.world().tuning().stepHz + 0.5f);
    for (int i = 0; i < steps; ++i) {
        sim.stepOnce();
    }
}

inline float horizontalDistance(const glm::vec3& a, const glm::vec3& b) {
    return glm::length(glm::vec2(a.x - b.x, a.z - b.z));
}

} // namespace woodyn::test
