#pragma once

#include <glm/vec3.hpp>

#include <array>
#include <cstdint>
#include <string_view>

namespace woodyn::materials {

/// Transparent color stain applied over the wood (D-26). Stain is VISUAL data
/// only: it never changes physics or sound (DESIGN.md §7). The wood grain stays
/// visible through it once grain rendering lands (Phase 3-4).
enum class Stain : std::uint8_t {
    Natural, // clear lacquer, no color
    Red,
    Yellow,
    Blue,
    Green,
    Orange,
    Purple,
};

inline constexpr std::array kAllStains{Stain::Natural, Stain::Red,    Stain::Yellow, Stain::Blue,
                                       Stain::Green,   Stain::Orange, Stain::Purple};

/// Linear-RGB color of the stain's dye.
glm::vec3 stainColor(Stain stain);

/// Albedo of stained wood (linear RGB). A real wood stain is a dye that colors
/// the fibers: the result takes the dye's hue, while the wood's own light/dark
/// variation (grain, earlywood/latewood) still shows through. Modeled as
///   dye x (wood luminance / reference luminance), blended by dye coverage.
/// `woodLinear` is the local wood color (later: sampled from the grain).
glm::vec3 applyStain(const glm::vec3& woodLinear, Stain stain);

std::string_view toString(Stain stain);

/// Next stain in the palette (wraps around).
Stain nextStain(Stain stain);

} // namespace woodyn::materials
