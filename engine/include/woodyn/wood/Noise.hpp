#pragma once

#include <glm/vec3.hpp>

#include <cstdint>

namespace woodyn::wood {

/// Deterministic 3D gradient noise in [-1, 1] (Perlin-style, hash based, no
/// tables), so grain is identical on every machine and run for a given seed.
float gradientNoise(const glm::vec3& p, std::uint32_t seed);

/// Fractal sum of `octaves` gradient-noise layers (lacunarity 2, gain 0.5).
float fbm(const glm::vec3& p, std::uint32_t seed, int octaves);

/// Integer hash to [0, 1).
float hash01(std::int32_t x, std::int32_t y, std::int32_t z, std::uint32_t seed);

} // namespace woodyn::wood
