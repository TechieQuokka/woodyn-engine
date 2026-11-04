#include "physics/ShapeFactory.hpp"

#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>

#include <algorithm>
#include <stdexcept>
#include <string>

namespace woodyn::physics::detail {
namespace {

JPH::Ref<JPH::ConvexHullShapeSettings> hullSettings(const geometry::ConvexPolyhedron& part,
                                                    const PhysicsTuning& tuning,
                                                    float internalDensity, float convexRadius) {
    const float s = tuning.lengthScale;
    JPH::Array<JPH::Vec3> points;
    points.reserve(part.vertices.size());
    for (const auto& v : part.vertices) {
        points.emplace_back(v.x * s, v.y * s, v.z * s);
    }
    JPH::Ref<JPH::ConvexHullShapeSettings> settings =
        new JPH::ConvexHullShapeSettings(points, convexRadius * s);
    settings->mMaxErrorConvexRadius = tuning.hullMaxErrorConvexRadius * s;
    settings->mHullTolerance = tuning.hullTolerance * s;
    settings->SetDensity(internalDensity);
    return settings;
}

JPH::RefConst<JPH::Shape> create(const JPH::ShapeSettings& settings, std::string_view what) {
    JPH::ShapeSettings::ShapeResult result = settings.Create();
    if (result.HasError()) {
        throw std::runtime_error(std::string("Jolt shape creation failed for ") +
                                 std::string(what) + ": " + result.GetError().c_str());
    }
    return result.Get();
}

} // namespace

float ShapeFactory::internalDensity(float densitySI) const {
    const float s = m_tuning.lengthScale;
    return densitySI / (s * s * s);
}

JPH::RefConst<JPH::Shape> ShapeFactory::blockShape(blocks::BlockType type, float densitySI) {
    const auto key = std::make_pair(type, densitySI);
    if (const auto it = m_cache.find(key); it != m_cache.end()) {
        return it->second;
    }
    auto shape = build(blocks::definition(type), densitySI);
    m_cache.emplace(key, shape);
    return shape;
}

JPH::RefConst<JPH::Shape> ShapeFactory::build(const blocks::BlockDefinition& def,
                                              float densitySI) const {
    using blocks::CollisionKind;
    const float s = m_tuning.lengthScale;
    const float density = internalDensity(densitySI);
    switch (def.collision) {
    case CollisionKind::Box: {
        const glm::vec3 h = def.halfExtents * s;
        const float radius = std::min({m_tuning.convexRadius * s, h.x, h.y, h.z});
        JPH::BoxShapeSettings settings(JPH::Vec3(h.x, h.y, h.z), radius);
        settings.SetDensity(density);
        return create(settings, def.name);
    }
    case CollisionKind::Cylinder: {
        const float r = def.cylinderRadius * s;
        const float hh = def.cylinderHalfHeight * s;
        const float radius = std::min({m_tuning.convexRadius * s, r, hh});
        JPH::CylinderShapeSettings settings(hh, r, radius);
        settings.SetDensity(density);
        return create(settings, def.name);
    }
    case CollisionKind::ConvexHull: {
        const auto settings =
            hullSettings(def.parts.front(), m_tuning, density, m_tuning.convexRadius);
        return create(*settings, def.name);
    }
    case CollisionKind::Compound: {
        JPH::StaticCompoundShapeSettings compound;
        for (const auto& part : def.parts) {
            // Part vertices are already in block-local coordinates.
            compound.AddShape(JPH::Vec3::sZero(), JPH::Quat::sIdentity(),
                              hullSettings(part, m_tuning, density, m_tuning.compoundPartConvexRadius));
        }
        return create(compound, def.name);
    }
    }
    throw std::logic_error("Unhandled CollisionKind");
}

} // namespace woodyn::physics::detail
