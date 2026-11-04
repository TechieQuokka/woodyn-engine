#pragma once

#include "woodyn/materials/Stain.hpp"
#include "woodyn/wood/WoodSource.hpp"

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cstdint>

namespace woodyn::wood {

/// Where a block was cut from (DESIGN.md §5.1-5.2): maps block-local
/// coordinates to the log's coordinates. It is fixed at cutting time, so the
/// grain moves with the block and never slides over its surface.
struct WoodCut {
    std::uint32_t source = 0;
    glm::vec3 center{0.0f};                     // block origin in log coordinates [m]
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; // block-local -> log

    [[nodiscard]] glm::vec3 toLog(const glm::vec3& local) const { return center + rotation * local; }
    /// Fiber direction expressed in block-local coordinates.
    [[nodiscard]] glm::vec3 localFiberAxis() const {
        return glm::conjugate(rotation) * WoodSource::fiberAxis();
    }
};

/// 0 for faces along the fibers (longitudinal grain), 1 for end grain, with a
/// smooth blend in between (DESIGN.md §5.3) so rounded edges do not show a seam.
float endGrainFactor(const WoodCut& cut, const glm::vec3& localNormal);

/// Final albedo (linear RGB) of a block surface point: grain from the log,
/// end-grain darkening, and the stain. End grain soaks up more dye, as on real
/// stained wood. This is the single source of truth for every renderer.
glm::vec3 surfaceAlbedo(const WoodSource& source, const WoodCut& cut, const glm::vec3& localPoint,
                        const glm::vec3& localNormal, materials::Stain stain);

} // namespace woodyn::wood
