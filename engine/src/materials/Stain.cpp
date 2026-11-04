#include "woodyn/materials/Stain.hpp"

#include <glm/geometric.hpp>

#include <cmath>

namespace woodyn::materials {
namespace {

glm::vec3 linear(float r, float g, float b) {
    return {std::pow(r, 2.2f), std::pow(g, 2.2f), std::pow(b, 2.2f)};
}

} // namespace

glm::vec3 stainColor(Stain stain) {
    // Classic wooden-toy palette, slightly softened (sRGB -> linear).
    switch (stain) {
    case Stain::Natural: return linear(0.87f, 0.71f, 0.53f); // beech-like reference
    case Stain::Red:     return linear(0.90f, 0.22f, 0.20f);
    case Stain::Yellow:  return linear(0.98f, 0.80f, 0.22f);
    case Stain::Blue:    return linear(0.22f, 0.50f, 0.88f);
    case Stain::Green:   return linear(0.34f, 0.70f, 0.30f);
    case Stain::Orange:  return linear(0.98f, 0.52f, 0.16f);
    case Stain::Purple:  return linear(0.60f, 0.36f, 0.82f);
    }
    return linear(0.87f, 0.71f, 0.53f);
}

glm::vec3 applyStain(const glm::vec3& woodLinear, Stain stain) {
    if (stain == Stain::Natural) {
        return woodLinear;
    }
    constexpr float kCoverage = 0.88f; // a little bare wood tone always remains
    const glm::vec3 lumaWeights{0.2126f, 0.7152f, 0.0722f};
    const float reference = glm::dot(stainColor(Stain::Natural), lumaWeights);
    const float variation = glm::dot(woodLinear, lumaWeights) / reference;
    const glm::vec3 dyed = stainColor(stain) * variation;
    return glm::mix(woodLinear, dyed, kCoverage);
}

std::string_view toString(Stain stain) {
    switch (stain) {
    case Stain::Natural: return "Natural";
    case Stain::Red:     return "Red";
    case Stain::Yellow:  return "Yellow";
    case Stain::Blue:    return "Blue";
    case Stain::Green:   return "Green";
    case Stain::Orange:  return "Orange";
    case Stain::Purple:  return "Purple";
    }
    return "Unknown";
}

Stain nextStain(Stain stain) {
    const auto i = static_cast<std::size_t>(stain);
    return kAllStains[(i + 1) % kAllStains.size()];
}

} // namespace woodyn::materials
