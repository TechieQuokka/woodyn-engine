#include "woodyn/core/FixedTimestep.hpp"

#include <algorithm>
#include <stdexcept>

namespace woodyn {

FixedTimestep::FixedTimestep(float stepSeconds, int maxStepsPerFrame)
    : m_step(stepSeconds), m_maxSteps(maxStepsPerFrame) {
    if (stepSeconds <= 0.0f || maxStepsPerFrame <= 0) {
        throw std::invalid_argument("FixedTimestep: step and max steps must be positive");
    }
}

int FixedTimestep::accumulate(float frameSeconds) {
    m_accumulator += std::max(frameSeconds, 0.0f);
    int steps = 0;
    while (m_accumulator >= m_step && steps < m_maxSteps) {
        m_accumulator -= m_step;
        ++steps;
    }
    if (steps == m_maxSteps && m_accumulator >= m_step) {
        // Drop the backlog instead of trying to catch up forever.
        m_accumulator = 0.0f;
    }
    return steps;
}

float FixedTimestep::alpha() const noexcept {
    return std::clamp(m_accumulator / m_step, 0.0f, 1.0f);
}

} // namespace woodyn
