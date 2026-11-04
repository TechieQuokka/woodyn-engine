#pragma once

#include <string_view>

namespace woodyn::materials {

/// Wood species. Physical and visual properties are stored separately and only
/// share this ID (DESIGN.md §7).
enum class WoodSpecies {
    Beech, // first species (D-07)
    Oak,   // second species, grain-continuity validation (D-07)
};

/// Physical (simulation) properties of a wood species.
///
/// Jolt uses a single friction coefficient per body (no separate static and
/// kinetic values) and combines two bodies' friction with the geometric mean.
/// Restitution is combined with the minimum (softer surface wins, D-29).
struct WoodPhysicalProperties {
    float density = 0.0f;     // [kg/m^3]
    float friction = 0.0f;    // Coulomb friction coefficient (wood on same wood)
    float restitution = 0.0f; // coefficient of restitution
    /// Rolling resistance coefficient c_rr (resisting torque = c_rr * N * r).
    /// Jolt has no rolling resistance; WOODYN applies it itself (D-21).
    float rollingResistance = 0.0f;
    bool provisional = true;  // true until calibrated against real samples (DESIGN.md §3.3)
};

const WoodPhysicalProperties& physicalProperties(WoodSpecies species);

std::string_view toString(WoodSpecies species);

} // namespace woodyn::materials
