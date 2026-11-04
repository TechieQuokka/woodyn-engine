#include "TestHelpers.hpp"

#include "woodyn/simulation/Scenes.hpp"
#include "woodyn/wood/WoodLibrary.hpp"

#include <glm/gtc/quaternion.hpp>
#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace woodyn;
using blocks::BlockType;

namespace {

float colorDistance(const glm::vec3& a, const glm::vec3& b) { return glm::length(a - b); }

/// World point -> block-local point for the block's current pose.
glm::vec3 toLocal(const physics::Transform& t, const glm::vec3& world) {
    return glm::conjugate(t.rotation) * (world - t.position);
}

} // namespace

// DESIGN.md §11: blocks cut from the same log continue each other's grain.
TEST(GrainContinuity, ConsecutiveCutsShareTheirSeamExactly) {
    wood::WoodLibrary lib;
    lib.startRun(materials::WoodSpecies::Beech);
    const auto a = lib.cut(BlockType::Brick, materials::WoodSpecies::Beech, wood::FiberAxis::X);
    const auto b = lib.cut(BlockType::Brick, materials::WoodSpecies::Beech, wood::FiberAxis::X);
    const auto& src = lib.source(a.source);
    ASSERT_EQ(a.source, b.source);
    const glm::vec3 h = blocks::definition(BlockType::Brick).halfExtents;
    for (float y : {-0.9f, -0.3f, 0.2f, 0.8f}) {
        for (float z : {-0.8f, 0.0f, 0.7f}) {
            const glm::vec3 onA{h.x, y * h.y, z * h.z};   // +X face of A
            const glm::vec3 onB{-h.x, y * h.y, z * h.z};  // -X face of B
            EXPECT_LT(glm::length(a.toLog(onA) - b.toLog(onB)), 1e-6f);
            const glm::vec3 n{1.0f, 0.0f, 0.0f};
            EXPECT_LT(colorDistance(wood::surfaceAlbedo(src, a, onA, n, materials::Stain::Natural),
                                    wood::surfaceAlbedo(src, b, onB, -n, materials::Stain::Natural)),
                      1e-3f) << "seam must be invisible (< 1/255 per channel)";
        }
    }
}

TEST(GrainContinuity, StackedTowerContinuesAcrossEveryJoint) {
    simulation::Simulation sim;
    const auto tower = simulation::scenes::buildTower(sim, 5);
    const float h = 0.5f * test::u;
    for (std::size_t i = 0; i + 1 < tower.size(); ++i) {
        const auto& lower = sim.blocks()[tower[i]].cut;
        const auto& upper = sim.blocks()[tower[i + 1]].cut;
        for (float x : {-0.7f, 0.1f, 0.6f}) {
            for (float z : {-0.5f, 0.4f}) {
                EXPECT_LT(glm::length(lower.toLog({x * h, h, z * h}) - upper.toLog({x * h, -h, z * h})), 1e-6f)
                    << "joint " << i;
            }
        }
    }
}

// DESIGN.md §11: grain stays fixed on the block when it moves or rotates.
TEST(GrainContinuity, GrainMovesWithTheBlockAndNeverSlides) {
    simulation::Simulation sim;
    const auto i = sim.addBlock(BlockType::Beam, materials::WoodSpecies::Beech, {{0.0f, 0.5f, 0.0f}, {}});
    const auto& block = sim.blocks()[i];
    const auto& src = sim.wood().source(block.cut.source);
    const physics::Transform before = sim.world().transform(block.body);

    // A material point on the side face, and the color seen there.
    const glm::vec3 local{0.031f, 0.004f, test::u * 0.5f};
    const glm::vec3 worldBefore = before.position + before.rotation * local;
    const glm::vec3 n{0.0f, 0.0f, 1.0f};
    const glm::vec3 seen = wood::surfaceAlbedo(src, block.cut, toLocal(before, worldBefore), n, materials::Stain::Natural);

    // Move and turn the block.
    const physics::Transform after{{0.2f, 0.3f, -0.1f}, glm::angleAxis(1.1f, glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f)))};
    sim.world().setTransform(block.body, after);
    const physics::Transform now = sim.world().transform(block.body);

    // The same material point (moved with the block) shows the same color...
    const glm::vec3 worldAfter = now.position + now.rotation * local;
    EXPECT_LT(colorDistance(seen, wood::surfaceAlbedo(src, block.cut, toLocal(now, worldAfter), n,
                                                      materials::Stain::Natural)), 1e-5f);
    // ...and the old world position no longer maps onto that point (sampling
    // in world space would make the grain slide).
    EXPECT_GT(glm::length(toLocal(now, worldBefore) - local), 0.01f);
}

TEST(GrainContinuity, EndGrainIsDetectedFromFiberDirection) {
    wood::WoodLibrary lib;
    const auto cut = lib.cut(BlockType::Cube, materials::WoodSpecies::Beech, wood::FiberAxis::Y);
    EXPECT_NEAR(wood::endGrainFactor(cut, {0, 1, 0}), 1.0f, 1e-5f);
    EXPECT_NEAR(wood::endGrainFactor(cut, {0, -1, 0}), 1.0f, 1e-5f);
    EXPECT_NEAR(wood::endGrainFactor(cut, {1, 0, 0}), 0.0f, 1e-5f);
    EXPECT_NEAR(wood::endGrainFactor(cut, {0, 0, 1}), 0.0f, 1e-5f);
    // Smooth blend over a rounded edge (no hard seam), monotonic in angle.
    float previous = -1.0f;
    for (int deg = 0; deg <= 90; deg += 5) {
        const float a = glm::radians(static_cast<float>(deg));
        const float f = wood::endGrainFactor(cut, {std::cos(a), std::sin(a), 0.0f});
        EXPECT_GE(f, previous - 1e-6f);
        previous = f;
    }
    const float mid = wood::endGrainFactor(cut, glm::normalize(glm::vec3(1, 1, 0)));
    EXPECT_GT(mid, 0.05f);
    EXPECT_LT(mid, 0.95f);
}

TEST(GrainContinuity, GrowthRingsHaveTheConfiguredSpacing) {
    const wood::WoodSource src(0, wood::defaultParams(materials::WoodSpecies::Oak, 5));
    const float spacing = src.params().ringWidth;
    // Count ring boundaries (phase crossing an integer) along a radius.
    int rings = 0;
    float last = src.sample({0.05f, 0.0f, 0.3f}).ringPhase;
    for (float r = 0.05f; r < 0.08f; r += 0.0001f) {
        const float phase = src.sample({r, 0.0f, 0.3f}).ringPhase;
        if (std::floor(phase) != std::floor(last)) ++rings;
        last = phase;
    }
    EXPECT_NEAR(static_cast<float>(rings), 0.03f / spacing, 0.25f * 0.03f / spacing);
}

TEST(GrainContinuity, CutsStayInsideTheLogAndNeverOverlap) {
    wood::WoodLibrary lib;
    struct Box { std::uint32_t source; glm::vec3 lo, hi; };
    std::vector<Box> boxes;
    for (int i = 0; i < 400; ++i) {
        const auto type = blocks::kAllBlockTypes[static_cast<std::size_t>(i) % blocks::kAllBlockTypes.size()];
        const auto cut = lib.cut(type, materials::WoodSpecies::Beech);
        const auto& src = lib.source(cut.source);
        const glm::vec3 h = blocks::definition(type).halfExtents;
        Box b{cut.source, glm::vec3(1e9f), glm::vec3(-1e9f)};
        for (int c = 0; c < 8; ++c) {
            const glm::vec3 corner{(c & 1) ? h.x : -h.x, (c & 2) ? h.y : -h.y, (c & 4) ? h.z : -h.z};
            const glm::vec3 p = cut.toLog(corner);
            EXPECT_TRUE(src.contains(p)) << "cut " << i << " pokes out of the log";
            b.lo = glm::min(b.lo, p);
            b.hi = glm::max(b.hi, p);
        }
        for (const auto& other : boxes) {
            if (other.source != b.source) continue;
            const glm::vec3 overlap = glm::min(b.hi, other.hi) - glm::max(b.lo, other.lo);
            const bool overlaps = overlap.x > 1e-5f && overlap.y > 1e-5f && overlap.z > 1e-5f;
            EXPECT_FALSE(overlaps) << "cut " << i << " overlaps an earlier cut";
        }
        boxes.push_back(b);
    }
    EXPECT_GE(lib.sourceCount(), 1u);
}

TEST(GrainContinuity, EveryBlockGetsItsOwnGrainButReproducibly) {
    wood::WoodLibrary a;
    wood::WoodLibrary b;
    const auto c1 = a.cut(BlockType::Cube, materials::WoodSpecies::Beech);
    for (int i = 0; i < 20; ++i) (void)a.cut(BlockType::Beam, materials::WoodSpecies::Beech);
    const auto c2 = a.cut(BlockType::Cube, materials::WoodSpecies::Beech);
    const auto d1 = b.cut(BlockType::Cube, materials::WoodSpecies::Beech);

    float sameDiff = 0.0f, otherDiff = 0.0f;
    const glm::vec3 n{0.0f, 0.0f, 1.0f};
    for (int k = 0; k < 64; ++k) {
        const glm::vec3 p{(static_cast<float>(k % 8) / 7.0f - 0.5f) * 0.028f,
                          (static_cast<float>(k / 8) / 7.0f - 0.5f) * 0.028f, 0.015f};
        const auto x1 = wood::surfaceAlbedo(a.source(c1.source), c1, p, n, materials::Stain::Natural);
        const auto y1 = wood::surfaceAlbedo(b.source(d1.source), d1, p, n, materials::Stain::Natural);
        const auto x2 = wood::surfaceAlbedo(a.source(c2.source), c2, p, n, materials::Stain::Natural);
        sameDiff += colorDistance(x1, y1);
        otherDiff += colorDistance(x1, x2);
    }
    EXPECT_EQ(sameDiff, 0.0f) << "same library sequence must give identical grain";
    EXPECT_GT(otherDiff / 64.0f, 0.004f) << "different cuts should look different";
}

TEST(GrainContinuity, SnapshotKeepsEachBlocksCut) {
    simulation::Simulation sim;
    simulation::scenes::buildHouse(sim);
    const auto saved = sim.snapshot();
    sim.restore(saved);
    for (std::size_t i = 0; i < saved.blocks.size(); ++i) {
        EXPECT_EQ(sim.blocks()[i].cut.source, saved.blocks[i].cut.source);
        EXPECT_LT(glm::length(sim.blocks()[i].cut.center - saved.blocks[i].cut.center), 1e-7f);
    }
}
