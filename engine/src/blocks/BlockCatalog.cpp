#include "woodyn/blocks/BlockCatalog.hpp"

#include "woodyn/core/Units.hpp"

#include <glm/gtc/constants.hpp>

#include <cmath>
#include <stdexcept>

namespace woodyn::blocks {
namespace {

using geometry::ConvexPolyhedron;
using units::kModule;

constexpr float u = kModule;
constexpr int kArchSegments = 12; // convex slices under the arch opening

const geometry::RoundingOptions kRounding{kEdgeRadius, 4, 30.0f};

BlockDefinition makeBase(BlockType type, std::string_view name, glm::vec3 sizeModules) {
    BlockDefinition def;
    def.type = type;
    def.name = name;
    def.sizeModules = sizeModules;
    def.halfExtents = sizeModules * (u * 0.5f);
    return def;
}

std::array<glm::vec2, 4> rectangle(float x0, float y0, float x1, float y1) {
    return {{{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}};
}

/// Rounded render mesh for a block extruded from a 2D profile along Z.
geometry::RenderMesh extrudedMesh(std::span<const glm::vec2> profileCCW, float depth) {
    const auto profile = geometry::filletProfile(profileCCW, kRounding);
    return geometry::roundedExtrusion(profile, depth, kRounding);
}

BlockDefinition makeCuboid(BlockType type, std::string_view name, glm::vec3 sizeModules) {
    BlockDefinition def = makeBase(type, name, sizeModules);
    def.collision = CollisionKind::Box;
    const glm::vec3 h = def.halfExtents;
    const auto rect = rectangle(-h.x, -h.y, h.x, h.y);
    def.parts.push_back(geometry::extrudeConvexPolygon(rect, 2.0f * h.z));
    def.renderMesh = extrudedMesh(rect, 2.0f * h.z);
    def.analyticVolume = 8.0f * h.x * h.y * h.z;
    return def;
}

BlockDefinition makeRoofPrism() {
    BlockDefinition def = makeBase(BlockType::RoofPrism, "RoofPrism", {2.0f, 1.0f, 1.0f});
    def.collision = CollisionKind::ConvexHull;
    const glm::vec3 h = def.halfExtents;
    const std::array<glm::vec2, 3> tri{{{-h.x, -h.y}, {h.x, -h.y}, {0.0f, h.y}}};
    def.parts.push_back(geometry::extrudeConvexPolygon(tri, 2.0f * h.z));
    def.renderMesh = extrudedMesh(tri, 2.0f * h.z);
    def.analyticVolume = 0.5f * (2.0f * h.x) * (2.0f * h.y) * (2.0f * h.z);
    return def;
}

BlockDefinition makeCylinder() {
    BlockDefinition def = makeBase(BlockType::Cylinder, "Cylinder", {1.0f, 2.0f, 1.0f});
    def.collision = CollisionKind::Cylinder;
    def.cylinderRadius = def.halfExtents.x;
    def.cylinderHalfHeight = def.halfExtents.y;
    const auto circle = geometry::circlePolygon(def.cylinderRadius, kCurveSegments);
    ConvexPolyhedron part = geometry::extrudeConvexPolygon(circle, 2.0f * def.cylinderHalfHeight);
    geometry::rotateZtoY(part);
    def.parts.push_back(std::move(part));
    def.renderMesh = geometry::roundedCylinder(def.cylinderRadius, def.cylinderHalfHeight, kRounding);
    def.analyticVolume = glm::pi<float>() * def.cylinderRadius * def.cylinderRadius *
                         2.0f * def.cylinderHalfHeight;
    return def;
}

std::vector<glm::vec2> halfDiscProfile(float r, float y0) {
    std::vector<glm::vec2> profile;
    for (int i = 0; i <= kCurveSegments; ++i) {
        const float a = glm::pi<float>() * static_cast<float>(i) / static_cast<float>(kCurveSegments);
        profile.emplace_back(r * std::cos(a), y0 + r * std::sin(a));
    }
    return profile;
}

BlockDefinition makeHalfCylinder() {
    BlockDefinition def = makeBase(BlockType::HalfCylinder, "HalfCylinder", {2.0f, 1.0f, 1.0f});
    def.collision = CollisionKind::ConvexHull;
    const float r = def.halfExtents.x; // = 1 module
    const auto profile = halfDiscProfile(r, -def.halfExtents.y);
    def.parts.push_back(geometry::extrudeConvexPolygon(profile, 2.0f * def.halfExtents.z));
    def.renderMesh = extrudedMesh(profile, 2.0f * def.halfExtents.z);
    def.analyticVolume = 0.5f * glm::pi<float>() * r * r * (2.0f * def.halfExtents.z);
    return def;
}

BlockDefinition makeArch() {
    BlockDefinition def = makeBase(BlockType::Arch, "Arch", {4.0f, 2.0f, 1.0f});
    def.collision = CollisionKind::Compound;
    const glm::vec3 h = def.halfExtents; // (2u, u, u/2)
    const float r = u;                   // opening radius
    const float depth = 2.0f * h.z;
    const float bottom = -h.y;
    const float spring = bottom + r;     // top of the opening (y = 0)

    // Collision decomposition. The whole top surface is ONE part so rounded
    // part edges never create grooves where other blocks rest or slide.
    const auto top = rectangle(-h.x, spring, h.x, h.y);
    def.parts.push_back(geometry::extrudeConvexPolygon(top, depth));
    for (const float side : {-1.0f, 1.0f}) {
        const float x0 = std::min(side * r, side * h.x);
        const float x1 = std::max(side * r, side * h.x);
        const auto leg = rectangle(x0, bottom, x1, spring);
        def.parts.push_back(geometry::extrudeConvexPolygon(leg, depth));
    }
    for (int i = 0; i < kArchSegments; ++i) {
        const float a0 = glm::pi<float>() * static_cast<float>(i) / static_cast<float>(kArchSegments);
        const float a1 = glm::pi<float>() * static_cast<float>(i + 1) / static_cast<float>(kArchSegments);
        const glm::vec2 p0{r * std::cos(a0), bottom + r * std::sin(a0)};
        const glm::vec2 p1{r * std::cos(a1), bottom + r * std::sin(a1)};
        if (spring - std::max(p0.y, p1.y) < 1e-4f && std::abs(p0.y - p1.y) < 1e-4f) {
            continue; // no material between arc and spring line
        }
        std::vector<glm::vec2> slice{p0, p1};
        if (spring - p1.y > 1e-6f) slice.emplace_back(p1.x, spring);
        if (spring - p0.y > 1e-6f) slice.emplace_back(p0.x, spring);
        def.parts.push_back(geometry::extrudeConvexPolygon(slice, depth));
    }

    // Render mesh: the concave outline extruded as one piece (no seams).
    std::vector<glm::vec2> outline{{-h.x, bottom}, {-r, bottom}};
    for (int i = kCurveSegments - 1; i >= 1; --i) { // arc over the opening, left to right
        const float a = glm::pi<float>() * static_cast<float>(i) / static_cast<float>(kCurveSegments);
        outline.emplace_back(r * std::cos(a), bottom + r * std::sin(a));
    }
    outline.insert(outline.end(), {{r, bottom}, {h.x, bottom}, {h.x, h.y}, {-h.x, h.y}});
    def.renderMesh = extrudedMesh(outline, depth);

    const float frontArea = (2.0f * h.x) * (2.0f * h.y) - 0.5f * glm::pi<float>() * r * r;
    def.analyticVolume = frontArea * depth;
    return def;
}

BlockDefinition makeCone() {
    BlockDefinition def = makeBase(BlockType::Cone, "Cone", {1.0f, 1.0f, 1.0f});
    def.collision = CollisionKind::ConvexHull;
    const float r = def.halfExtents.x;
    const float height = 2.0f * def.halfExtents.y;
    const auto base = geometry::circlePolygon(r, kCurveSegments);
    def.parts.push_back(geometry::makePyramid(base, height));
    def.renderMesh = geometry::roundedCone(r, height, kRounding);
    def.analyticVolume = glm::pi<float>() * r * r * height / 3.0f;
    return def;
}

BlockDefinition makePyramid() {
    BlockDefinition def = makeBase(BlockType::Pyramid, "Pyramid", {1.0f, 1.0f, 1.0f});
    def.collision = CollisionKind::ConvexHull;
    const glm::vec3 h = def.halfExtents;
    const std::array<glm::vec2, 4> base{{{-h.x, -h.z}, {h.x, -h.z}, {h.x, h.z}, {-h.x, h.z}}};
    def.parts.push_back(geometry::makePyramid(base, 2.0f * h.y));

    // A right square pyramid has an insphere: shrink about its center, then round.
    const float height = 2.0f * h.y;
    const float inradius = h.x * height / (h.x + std::sqrt(h.x * h.x + height * height));
    const glm::vec3 incenter{0.0f, -h.y + inradius, 0.0f};
    const auto core = geometry::shrinkTangential(def.parts.front(), incenter, inradius, kEdgeRadius);
    def.renderMesh = geometry::minkowskiRound(core, kEdgeRadius, kRounding.segmentsPer90);

    def.analyticVolume = (2.0f * h.x) * (2.0f * h.z) * (2.0f * h.y) / 3.0f;
    return def;
}

std::vector<BlockDefinition> buildCatalog() {
    std::vector<BlockDefinition> all;
    all.push_back(makeCuboid(BlockType::Cube, "Cube", {1.0f, 1.0f, 1.0f}));
    all.push_back(makeCuboid(BlockType::Brick, "Brick", {2.0f, 1.0f, 1.0f}));
    all.push_back(makeCuboid(BlockType::Beam, "Beam", {4.0f, 1.0f, 1.0f}));
    all.push_back(makeRoofPrism());
    all.push_back(makeCylinder());
    all.push_back(makeHalfCylinder());
    all.push_back(makeArch());
    all.push_back(makeCone());
    all.push_back(makePyramid());
    return all;
}

} // namespace

const BlockDefinition& definition(BlockType type) {
    static const std::vector<BlockDefinition> catalog = buildCatalog();
    for (const auto& def : catalog) {
        if (def.type == type) {
            return def;
        }
    }
    throw std::out_of_range("Unknown BlockType");
}

std::string_view toString(BlockType type) {
    return definition(type).name;
}

} // namespace woodyn::blocks
