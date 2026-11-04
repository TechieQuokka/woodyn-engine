#include "woodyn/wood/WoodSource.hpp"

#include "woodyn/wood/Noise.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace woodyn::wood {
namespace {

glm::vec3 lin(float r, float g, float b) {
    return {std::pow(r, 2.2f), std::pow(g, 2.2f), std::pow(b, 2.2f)};
}

float smoothstep(float a, float b, float x) {
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

WoodSourceParams defaultParams(materials::WoodSpecies species, std::uint32_t seed) {
    WoodSourceParams p;
    p.species = species;
    p.seed = seed;
    // Deterministic, slightly off-center pith per log.
    p.pithOffset = {0.012f * (hash01(1, 2, 3, seed) - 0.5f), 0.012f * (hash01(4, 5, 6, seed) - 0.5f)};
    switch (species) {
    case materials::WoodSpecies::Beech:
        // Diffuse-porous: subtle rings, prominent small ray flecks, pinkish tone.
        p.ringWidth = 0.0028f;
        p.ringContrast = 0.24f;
        p.fiberNoise = 0.03f;
        p.rayDensity = 0.35f;
        p.rayDarkening = 0.16f;
        p.poreBand = 0.0f;
        p.earlywood = lin(0.89f, 0.73f, 0.56f);
        p.latewood = lin(0.80f, 0.60f, 0.43f);
        break;
    case materials::WoodSpecies::Oak:
        // Ring-porous: clear rings with a band of large earlywood pores, big rays.
        p.ringWidth = 0.0032f;
        p.ringContrast = 0.42f;
        p.fiberNoise = 0.04f;
        p.rayDensity = 0.12f;
        p.rayDarkening = 0.10f;
        p.poreBand = 0.6f;
        p.earlywood = lin(0.80f, 0.65f, 0.45f);
        p.latewood = lin(0.62f, 0.46f, 0.30f);
        break;
    }
    return p;
}

WoodSource::WoodSource(std::uint32_t id, const WoodSourceParams& params) : m_id(id), m_params(params) {}

bool WoodSource::contains(const glm::vec3& p) const {
    constexpr float kEps = 1e-6f; // tolerate float rounding of exact boundary cuts
    return glm::length(glm::vec2(p.x, p.y)) <= m_params.radius + kEps && p.z >= -kEps &&
           p.z <= m_params.length + kEps;
}

GrainSample WoodSource::sample(const glm::vec3& p) const {
    const auto& s = m_params;
    const std::uint32_t seed = s.seed;
    const glm::vec2 xy = glm::vec2(p.x, p.y) - s.pithOffset;

    // Rings: distance from the pith, distorted by slow 3D wobble (rings are
    // never perfect circles and drift along the length of the log).
    const float wobble = s.ringWobble * fbm(glm::vec3(xy * 18.0f, p.z * 4.0f), seed, 3);
    const float r = glm::length(xy) + wobble;
    const float phase = r / s.ringWidth;
    const float f = phase - std::floor(phase);

    // Within a ring: light earlywood, then denser latewood ending abruptly.
    float late = smoothstep(0.55f, 0.92f, f) * (1.0f - smoothstep(0.97f, 1.0f, f));
    // Ring width varies year to year.
    late *= 0.75f + 0.5f * hash01(static_cast<std::int32_t>(std::floor(phase)), 0, 0, seed);

    glm::vec3 color = glm::mix(s.earlywood, s.latewood, std::clamp(late * s.ringContrast / 0.35f, 0.0f, 1.0f));

    // Ring-porous species: a band of large pores at the start of each ring.
    if (s.poreBand > 0.0f && f < 0.18f) {
        const float pores = gradientNoise(glm::vec3(xy * 2600.0f, p.z * 90.0f), seed + 7u);
        if (pores > 0.25f) color *= 1.0f - 0.35f * s.poreBand;
    }

    // Medullary rays: thin radial ribbons, a few mm tall along the fibers.
    if (s.rayDensity > 0.0f && r > 0.002f) {
        const float angle = std::atan2(xy.y, xy.x);
        const float across = angle * r / 0.00045f; // ~0.45 mm wide
        const auto cell = static_cast<std::int32_t>(std::floor(across));
        const float local = across - std::floor(across);
        const auto zCell = static_cast<std::int32_t>(std::floor(p.z / 0.004f + hash01(cell, 9, 9, seed)));
        const auto rCell = static_cast<std::int32_t>(std::floor(r / 0.01f));
        if (hash01(cell, zCell, rCell, seed + 3u) < s.rayDensity && std::abs(local - 0.5f) < 0.2f) {
            color *= 1.0f - s.rayDarkening;
            color.r *= 1.04f; // rays are slightly redder in beech
        }
    }

    // Fine fiber streaks: noise stretched along +Z.
    // Two scales: soft streaks (~3 mm) plus faint fine texture (~0.8 mm).
    const float streak = 0.7f * gradientNoise(glm::vec3(xy * 330.0f, p.z * 12.0f), seed + 11u) +
                         0.3f * gradientNoise(glm::vec3(xy * 1250.0f, p.z * 40.0f), seed + 13u);
    color *= 1.0f + s.fiberNoise * streak;
    // Large-scale color variation within the log.
    color *= 1.0f + 0.06f * fbm(p * 6.0f, seed + 17u, 2);

    return {glm::clamp(color, glm::vec3(0.0f), glm::vec3(1.0f)), phase, late};
}

} // namespace woodyn::wood
