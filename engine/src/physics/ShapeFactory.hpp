#pragma once

// Private header: converts WOODYN block definitions into Jolt shapes.

#include "woodyn/blocks/BlockCatalog.hpp"
#include "woodyn/physics/PhysicsTuning.hpp"

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Collision/Shape/Shape.h>

#include <map>
#include <utility>

namespace woodyn::physics::detail {

/// Builds and caches one Jolt shape per (block type, density) pair.
/// Shapes are created in Jolt internal units (meters x tuning.lengthScale) with
/// a density chosen so that Jolt's mass is in kilograms.
class ShapeFactory {
public:
    explicit ShapeFactory(const PhysicsTuning& tuning) : m_tuning(tuning) {}

    /// @param densitySI  density in kg/m^3
    JPH::RefConst<JPH::Shape> blockShape(blocks::BlockType type, float densitySI);

    /// Density in Jolt internal units that yields SI masses.
    [[nodiscard]] float internalDensity(float densitySI) const;

private:
    JPH::RefConst<JPH::Shape> build(const blocks::BlockDefinition& def, float densitySI) const;

    PhysicsTuning m_tuning;
    std::map<std::pair<blocks::BlockType, float>, JPH::RefConst<JPH::Shape>> m_cache;
};

} // namespace woodyn::physics::detail
