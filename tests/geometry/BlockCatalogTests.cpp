#include "TestHelpers.hpp"

#include "woodyn/blocks/BlockCatalog.hpp"

#include <gtest/gtest.h>

#include <cmath>

using namespace woodyn;
using blocks::BlockType;

class BlockCatalogTest : public ::testing::TestWithParam<BlockType> {};

TEST_P(BlockCatalogTest, DimensionsAreWholeModules) {
    const auto& def = blocks::definition(GetParam());
    for (int axis = 0; axis < 3; ++axis) {
        const float m = def.sizeModules[axis];
        EXPECT_FLOAT_EQ(m, std::round(m)) << def.name << " axis " << axis;
        EXPECT_NEAR(def.halfExtents[axis] * 2.0f, m * test::u, 1e-6f);
    }
}

TEST_P(BlockCatalogTest, PartsStayInsideBoundingBox) {
    const auto& def = blocks::definition(GetParam());
    ASSERT_FALSE(def.parts.empty());
    for (const auto& part : def.parts) {
        for (const auto& v : part.vertices) {
            for (int axis = 0; axis < 3; ++axis) {
                EXPECT_LE(std::abs(v[axis]), def.halfExtents[axis] + 1e-6f) << def.name;
            }
        }
    }
}

TEST_P(BlockCatalogTest, FacesPointOutward) {
    const auto& def = blocks::definition(GetParam());
    for (const auto& part : def.parts) {
        const glm::vec3 c = part.vertexCentroid();
        for (const auto& face : part.faces) {
            ASSERT_GE(face.size(), 3u);
            const glm::vec3 a = part.vertices[face[0]];
            const glm::vec3 b = part.vertices[face[1]];
            const glm::vec3 d = part.vertices[face[2]];
            const glm::vec3 n = glm::cross(b - a, d - a);
            EXPECT_GT(glm::dot(n, a - c), 0.0f) << def.name;
        }
    }
}

TEST_P(BlockCatalogTest, PolyhedralVolumeMatchesAnalyticShape) {
    const auto& def = blocks::definition(GetParam());
    float volume = 0.0f;
    for (const auto& part : def.parts) {
        volume += part.volume();
    }
    ASSERT_GT(def.analyticVolume, 0.0f);
    // Curved surfaces are approximated with 24 segments (~1.2% area loss).
    EXPECT_NEAR(volume / def.analyticVolume, 1.0f, 0.02f) << def.name;
}

INSTANTIATE_TEST_SUITE_P(AllBlocks, BlockCatalogTest, ::testing::ValuesIn(blocks::kAllBlockTypes),
                         [](const ::testing::TestParamInfo<BlockType>& paramInfo) {
                             return std::string(blocks::toString(paramInfo.param));
                         });

TEST(BlockCatalog, ArchIsCompoundOfConvexParts) {
    const auto& arch = blocks::definition(BlockType::Arch);
    EXPECT_EQ(arch.collision, blocks::CollisionKind::Compound);
    EXPECT_GT(arch.parts.size(), 2u);
}

TEST(BlockCatalog, TriangulationProducesTriangles) {
    for (const BlockType type : blocks::kAllBlockTypes) {
        const auto mesh = geometry::triangulate(blocks::definition(type).parts);
        EXPECT_FALSE(mesh.indices.empty());
        EXPECT_EQ(mesh.indices.size() % 3, 0u);
    }
}

TEST(BlockCatalog, SurfaceMeshDropsArchInternalFaces) {
    const auto& arch = blocks::definition(BlockType::Arch);
    const auto all = geometry::triangulate(arch.parts);
    const auto surface = geometry::triangulateSurface(arch.parts);
    EXPECT_LT(surface.indices.size(), all.indices.size());
    // Convex single-part blocks keep every face.
    const auto& cube = blocks::definition(BlockType::Cube);
    EXPECT_EQ(geometry::triangulateSurface(cube.parts).indices.size(),
              geometry::triangulate(cube.parts).indices.size());
}

// --- Rounded render meshes ---------------------------------------------------

class RenderMeshTest : public ::testing::TestWithParam<BlockType> {};

TEST_P(RenderMeshTest, IsClosedWithVolumeCloseToIdealShape) {
    const auto& def = blocks::definition(GetParam());
    const float volume = def.renderMesh.volume();
    // Edge rounding removes < 2% of the volume of a 3 cm block; facets ~1%.
    EXPECT_NEAR(volume / def.analyticVolume, 0.985f, 0.025f) << def.name;
}

TEST_P(RenderMeshTest, FitsTheBoundingBox) {
    const auto& def = blocks::definition(GetParam());
    glm::vec3 lo{1e9f};
    glm::vec3 hi{-1e9f};
    for (const auto& p : def.renderMesh.positions) {
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
    }
    // Rounding a sharp corner (< 90 deg: roof eaves, cone/pyramid apex) pulls the
    // surface inside the bounding box by r * (1 / sin(half angle) - 1), exactly
    // like Jolt's convex radius does for the collision shape.
    const bool sharpCorners = def.type == BlockType::RoofPrism || def.type == BlockType::Cone ||
                              def.type == BlockType::Pyramid;
    const float slack = sharpCorners ? 2.5e-3f : 2e-4f;
    for (int axis = 0; axis < 3; ++axis) {
        EXPECT_LE(hi[axis], def.halfExtents[axis] + 1e-5f) << def.name << " axis " << axis;
        EXPECT_GE(lo[axis], -def.halfExtents[axis] - 1e-5f) << def.name << " axis " << axis;
        EXPECT_GE(hi[axis], def.halfExtents[axis] - slack) << def.name << " axis " << axis;
        EXPECT_LE(lo[axis], -def.halfExtents[axis] + slack) << def.name << " axis " << axis;
    }
}

TEST_P(RenderMeshTest, NormalsAreUnitAndAgreeWithWinding) {
    const auto& m = blocks::definition(GetParam()).renderMesh;
    ASSERT_EQ(m.positions.size(), m.normals.size());
    for (const auto& n : m.normals) {
        EXPECT_NEAR(glm::length(n), 1.0f, 1e-4f);
    }
    for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        const auto a = m.indices[i], b = m.indices[i + 1], c = m.indices[i + 2];
        const glm::vec3 g = glm::cross(m.positions[b] - m.positions[a], m.positions[c] - m.positions[a]);
        EXPECT_GE(glm::dot(g, m.normals[a] + m.normals[b] + m.normals[c]), 0.0f);
    }
}

INSTANTIATE_TEST_SUITE_P(AllBlocks, RenderMeshTest, ::testing::ValuesIn(blocks::kAllBlockTypes),
                         [](const ::testing::TestParamInfo<BlockType>& paramInfo) {
                             return std::string(blocks::toString(paramInfo.param));
                         });
