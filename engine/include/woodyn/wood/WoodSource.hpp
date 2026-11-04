#pragma once

#include "woodyn/materials/WoodSpecies.hpp"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>

namespace woodyn::wood {

/// Parameters of one log (DESIGN.md §5.1). Log coordinate system: +Z along
/// the fibers (pith axis), origin on the pith at one end; X/Y span the cross
/// section. Lengths in meters, colors in linear RGB.
struct WoodSourceParams {
    materials::WoodSpecies species = materials::WoodSpecies::Beech;
    std::uint32_t seed = 1;
    float length = 1.2f;             // [m]
    float radius = 0.18f;            // [m] 36 cm diameter log
    glm::vec2 pithOffset{0.0f};      // pith is rarely exactly centered
    float ringWidth = 0.0028f;       // [m] mean growth ring width
    float ringWobble = 0.0012f;      // [m] low-frequency ring distortion
    float ringContrast = 0.35f;      // 0..1 latewood darkening
    float rayDensity = 0.30f;        // medullary ray flecks (beech: prominent)
    float rayDarkening = 0.18f;
    float poreBand = 0.0f;           // ring-porous earlywood pores (oak)
    float fiberNoise = 0.05f;        // fine streaks along the fibers
    glm::vec3 earlywood{0.74f, 0.47f, 0.25f};
    glm::vec3 latewood{0.55f, 0.32f, 0.16f};
};

/// Default parameters for a species (PROVISIONAL, to be matched to photos/
/// samples in Phase 4: D-08 textures).
WoodSourceParams defaultParams(materials::WoodSpecies species, std::uint32_t seed);

/// Result of sampling the grain field.
struct GrainSample {
    glm::vec3 color{0.0f}; // linear RGB albedo of the bare wood
    float ringPhase = 0.0f; // ring count from the pith (continuous)
    float latewood = 0.0f;  // 0 earlywood .. 1 latewood
};

/// A log / raw piece of wood: a deterministic 3D grain field. Because the field
/// is defined in 3D log coordinates, every cut through it (side faces, end
/// grain, rounded edges) shows the matching pattern, and blocks cut next to
/// each other continue each other's grain (DESIGN.md §5).
class WoodSource {
public:
    WoodSource(std::uint32_t id, const WoodSourceParams& params);

    [[nodiscard]] std::uint32_t id() const noexcept { return m_id; }
    [[nodiscard]] const WoodSourceParams& params() const noexcept { return m_params; }

    /// Fiber direction in log coordinates (constant +Z for a straight log).
    [[nodiscard]] static glm::vec3 fiberAxis() noexcept { return {0.0f, 0.0f, 1.0f}; }

    [[nodiscard]] GrainSample sample(const glm::vec3& logPoint) const;

    /// Whether a point is inside the usable wood (inside the bark, within length).
    [[nodiscard]] bool contains(const glm::vec3& logPoint) const;

private:
    std::uint32_t m_id;
    WoodSourceParams m_params;
};

} // namespace woodyn::wood
