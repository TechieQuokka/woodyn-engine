#include "woodyn/wood/WoodCut.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace woodyn::wood {

float endGrainFactor(const WoodCut& cut, const glm::vec3& localNormal) {
    const float alignment = std::abs(glm::dot(glm::normalize(localNormal), cut.localFiberAxis()));
    const float t = std::clamp((alignment - 0.55f) / (0.85f - 0.55f), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

glm::vec3 surfaceAlbedo(const WoodSource& source, const WoodCut& cut, const glm::vec3& localPoint,
                        const glm::vec3& localNormal, materials::Stain stain) {
    const GrainSample g = source.sample(cut.toLog(localPoint));
    const float endGrain = endGrainFactor(cut, localNormal);
    // Open fiber ends look darker and absorb more stain.
    glm::vec3 wood = g.color * (1.0f - 0.16f * endGrain);
    if (stain == materials::Stain::Natural) {
        return wood;
    }
    const glm::vec3 stained = materials::applyStain(wood, stain);
    // Extra dye uptake on end grain: push further toward (darker) dye color.
    const glm::vec3 soaked = stained * 0.85f;
    return glm::mix(stained, soaked, endGrain);
}

} // namespace woodyn::wood
