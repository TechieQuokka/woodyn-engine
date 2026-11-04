#pragma once

// SI units throughout (DESIGN.md §3.2): meters, kilograms, seconds.

namespace woodyn::units {

/// Base module of the block set (D-06). Every block dimension is a multiple of this.
inline constexpr float kModule = 0.03f; // 3 cm

/// Standard gravity [m/s^2].
inline constexpr float kStandardGravity = 9.80665f;

/// Default physics update rate [Hz] (D-11). DESIGN.md §3.2 started at 120 Hz,
/// but 3 cm blocks have a natural time scale sqrt(L/g) of ~55 ms, so 120 Hz gives
/// only ~7 steps per characteristic time and towers of 7+ cubes collapse.
/// 480 Hz keeps 15-cube towers stable at ~1.2 ms per step for 1,000 blocks.
inline constexpr float kDefaultPhysicsRateHz = 480.0f;

/// Converts a length in modules to meters.
constexpr float modules(float count) noexcept { return count * kModule; }

} // namespace woodyn::units
