#pragma once

#include "woodyn/geometry/Polyhedron.hpp"
#include "woodyn/geometry/RoundedMesh.hpp"

#include <glm/vec3.hpp>

#include <array>
#include <span>
#include <string_view>
#include <vector>

namespace woodyn::blocks {

/// Block types of the initial toy set (D-05). Sizes follow the 3 cm module (D-06).
enum class BlockType {
    Cube,           // 1 x 1 x 1 module
    Brick,          // 2 x 1 x 1
    Beam,           // 4 x 1 x 1
    RoofPrism,      // triangular prism, 2 wide x 1 high x 1 deep
    Cylinder,       // diameter 1, height 2
    HalfCylinder,   // 2 wide x 1 high x 1 deep, flat side down
    Arch,           // 4 wide x 2 high x 1 deep, semicircular opening radius 1
    Cone,           // diameter 1, height 1
    Pyramid,        // square base 1 x 1, height 1
};

inline constexpr std::array kAllBlockTypes{
    BlockType::Cube,     BlockType::Brick,        BlockType::Beam,
    BlockType::RoofPrism, BlockType::Cylinder,    BlockType::HalfCylinder,
    BlockType::Arch,     BlockType::Cone,         BlockType::Pyramid,
};

/// How the physics engine should represent a block.
enum class CollisionKind {
    Box,        // analytic box primitive
    Cylinder,   // analytic cylinder primitive (axis = +Y)
    ConvexHull, // single convex hull built from `parts[0]`
    Compound,   // several convex hulls (concave blocks such as the arch)
};

/// Immutable description of one block type. All lengths in meters, local frame:
/// +Y up, origin at the center of the bounding box.
struct BlockDefinition {
    BlockType type{};
    std::string_view name;
    glm::vec3 sizeModules{1.0f};        // bounding box in modules (x, y, z)
    glm::vec3 halfExtents{0.0f};        // bounding box half extents [m]
    CollisionKind collision{};
    float cylinderRadius = 0.0f;        // for CollisionKind::Cylinder
    float cylinderHalfHeight = 0.0f;    // for CollisionKind::Cylinder
    std::vector<geometry::ConvexPolyhedron> parts; // convex decomposition (collision)
    geometry::RenderMesh renderMesh;               // rounded-edge visual mesh, smooth normals

    /// Exact (analytic) volume of the ideal shape [m^3], used for validation.
    float analyticVolume = 0.0f;
};

/// Number of segments used to approximate curved surfaces.
inline constexpr int kCurveSegments = 24;

/// Edge rounding radius of the machined blocks [m]. Real toy blocks have
/// ~1.5-2 mm rounded edges; collision uses the same value as Jolt convex radius.
inline constexpr float kEdgeRadius = 0.0015f;

/// Returns the definition for a block type. Definitions are built once and cached.
const BlockDefinition& definition(BlockType type);

std::string_view toString(BlockType type);

} // namespace woodyn::blocks
