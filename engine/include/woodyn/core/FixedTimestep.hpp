#pragma once

namespace woodyn {

/// Fixed-timestep accumulator that decouples simulation rate from frame rate
/// (DESIGN.md §3.2, §7).
class FixedTimestep {
public:
    /// @param stepSeconds  fixed simulation step [s]
    /// @param maxStepsPerFrame  cap to avoid a "spiral of death" after long frames
    explicit FixedTimestep(float stepSeconds, int maxStepsPerFrame = 8);

    /// Adds elapsed real time and returns how many fixed steps to run now.
    int accumulate(float frameSeconds);

    /// Interpolation factor in [0, 1) between the previous and current state.
    [[nodiscard]] float alpha() const noexcept;

    [[nodiscard]] float stepSeconds() const noexcept { return m_step; }

    void reset() noexcept { m_accumulator = 0.0f; }

private:
    float m_step;
    int m_maxSteps;
    float m_accumulator = 0.0f;
};

} // namespace woodyn
