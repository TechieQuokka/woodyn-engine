#pragma once

// Render meshes with rounded (filleted) edges, as on real machined toy blocks.
// The rounding matches the collision shapes: Jolt's convex radius is exactly a
// Minkowski sum with a sphere, which is what these meshes model.

#include "woodyn/geometry/Polyhedron.hpp"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace woodyn::geometry {

/// Triangle mesh with per-vertex normals (smooth where the surface is curved).
struct RenderMesh {
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<std::uint32_t> indices;

    /// Enclosed volume (divergence theorem). Requires a closed, outward mesh.
    [[nodiscard]] float volume() const;
    void append(const RenderMesh& other);
};

/// A 2D profile point with its outward unit normal and the factor by which an
/// inward offset must be scaled to keep adjacent edges parallel (1 / cos).
struct ProfilePoint {
    glm::vec2 position;
    glm::vec2 normal;
    float miter = 1.0f;
};

struct RoundingOptions {
    float radius = 0.0015f;          // edge radius [m]
    int segmentsPer90 = 4;           // arc resolution
    float smoothAngleDegrees = 30.0f; // profile turns below this are shaded smooth, not filleted
};

/// Fillets the convex corners of a simple CCW polygon (may be concave; concave
/// corners stay sharp). Shallow turns (e.g. a polygonal circle) become smooth.
std::vector<ProfilePoint> filletProfile(std::span<const glm::vec2> polygonCCW,
                                        const RoundingOptions& options);

/// Extrudes a filleted profile (XY plane) along Z and rounds the cap edges.
RenderMesh roundedExtrusion(std::span<const ProfilePoint> profile, float depth,
                            const RoundingOptions& options);

/// Builds a surface of revolution around +Y from an open profile polyline in
/// the (radius, y) half plane that starts and ends on the axis.
RenderMesh revolveProfile(std::span<const ProfilePoint> profileRY, int segments);

/// Rounded cylinder (axis +Y) and cone (apex +Y) built by revolution.
RenderMesh roundedCylinder(float radius, float halfHeight, const RoundingOptions& options,
                           int segments = 48);
RenderMesh roundedCone(float radius, float height, const RoundingOptions& options,
                       int segments = 48);

/// Minkowski sum of a convex polyhedron with a sphere: flat offset faces,
/// cylindrical edge strips and spherical corner patches.
RenderMesh minkowskiRound(const ConvexPolyhedron& core, float radius, int segmentsPer90 = 4);

/// Shrinks a tangential polyhedron (one with an insphere) by `radius` so that
/// minkowskiRound(result, radius) keeps the original outer size.
ConvexPolyhedron shrinkTangential(const ConvexPolyhedron& poly, const glm::vec3& incenter,
                                  float inradius, float radius);

/// Ear-clipping triangulation of a simple CCW polygon. Returns index triples.
std::vector<std::uint32_t> triangulatePolygon(std::span<const glm::vec2> polygonCCW);

} // namespace woodyn::geometry
