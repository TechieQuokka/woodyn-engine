#include "woodyn/materials/WoodSpecies.hpp"

namespace woodyn::materials {
namespace {

// PROVISIONAL literature-range values (air-dried, finished wood). They are
// placeholders until measured on real samples (DESIGN.md §3.3, §11).
//  - Density: European beech ~680-750 kg/m^3, European oak ~650-760 kg/m^3.
//  - Friction: dry wood on wood is typically 0.25-0.5.
//  - Restitution: small hardwood blocks on hardwood, roughly 0.4-0.6.
//  - Rolling resistance: hard wood on hard wood ~0.001-0.01; hand-made toy
//    blocks are never perfectly round, so the upper part of the range is used.
constexpr WoodPhysicalProperties kBeech{720.0f, 0.45f, 0.50f, 0.006f, true};
constexpr WoodPhysicalProperties kOak{710.0f, 0.50f, 0.45f, 0.007f, true};

} // namespace

const WoodPhysicalProperties& physicalProperties(WoodSpecies species) {
    switch (species) {
    case WoodSpecies::Beech: return kBeech;
    case WoodSpecies::Oak:   return kOak;
    }
    return kBeech;
}

std::string_view toString(WoodSpecies species) {
    switch (species) {
    case WoodSpecies::Beech: return "Beech";
    case WoodSpecies::Oak:   return "Oak";
    }
    return "Unknown";
}

} // namespace woodyn::materials
