#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace woodyn::geometry {

/// A convex polyhedron described by vertices and polygonal faces.
/// Faces are vertex index loops, counter-clockwise when seen from outside.
/// The same data feeds both the physics convex hull and the render mesh,
/// so collision and visuals always agree.
struct ConvexPolyhedron {
    std::vector<glm::vec3> vertices;
    std::vector<std::vector<std::uint32_t>> faces;

    [[nodiscard]] glm::vec3 vertexCentroid() const;
    [[nodiscard]] float volume() const;
};

/// Triangle list (3 indices per triangle) with flat per-triangle normals implied.
struct TriangleMesh {
    std::vector<glm::vec3> positions;
    std::vector<std::uint32_t> indices;
};

/// Extrudes a convex 2D polygon (in the XY plane) symmetrically along Z.
/// @param polygon  convex polygon, any winding
/// @param depth    total extrusion depth [m]
ConvexPolyhedron extrudeConvexPolygon(std::span<const glm::vec2> polygon, float depth);

/// Builds a pyramid/cone: a convex base polygon in the XZ plane at y = -height/2
/// and an apex at (0, +height/2, 0).
/// @param basePolygonXZ  convex polygon in XZ (x -> x, y -> z)
ConvexPolyhedron makePyramid(std::span<const glm::vec2> basePolygonXZ, float height);

/// Regular polygon approximation of a circle (CCW), starting at angle 0.
std::vector<glm::vec2> circlePolygon(float radius, int segments);

/// Re-orders every face so it is counter-clockwise seen from outside.
/// Valid for convex polyhedra (uses the vertex centroid as interior point).
void orientFacesOutward(ConvexPolyhedron& poly);

/// Applies a rigid rotation that maps the +Z axis onto +Y (rotation of -90 deg about X).
void rotateZtoY(ConvexPolyhedron& poly);

/// Fan-triangulates all faces into a triangle mesh.
TriangleMesh triangulate(std::span<const ConvexPolyhedron> parts);

/// Like `triangulate`, but drops internal faces where two convex parts touch
/// (coincident faces with opposite orientation), leaving only the outer surface.
/// Used for rendering compound blocks such as the arch.
TriangleMesh triangulateSurface(std::span<const ConvexPolyhedron> parts, float epsilon = 1e-6f);

} // namespace woodyn::geometry
