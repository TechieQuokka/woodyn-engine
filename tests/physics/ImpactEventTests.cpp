#include "TestHelpers.hpp"

#include <gtest/gtest.h>

#include <cmath>

using namespace woodyn;

TEST(ImpactEvents, DropOnMatReportsImpactSpeed) {
    simulation::Simulation sim;
    sim.addPlayArea();
    const float h = 0.3f;
    const auto i = sim.addBlock(blocks::BlockType::Cube, materials::WoodSpecies::Beech,
                                {{0.0f, h + 0.5f * test::u, 0.0f}, {}});
    std::vector<simulation::SoundImpact> impacts;
    for (int step = 0; step < 480; ++step) {
        sim.stepOnce();
        for (auto& e : sim.takeImpacts()) impacts.push_back(e);
    }
    ASSERT_FALSE(impacts.empty());
    const auto& first = impacts.front();
    const bool blockVsMat = (first.a.block == i && !first.b.block && first.b.surface == audio::SurfaceKind::PlayMat) ||
                            (first.b.block == i && !first.a.block && first.a.surface == audio::SurfaceKind::PlayMat);
    EXPECT_TRUE(blockVsMat);
    const float expected = std::sqrt(2.0f * units::kStandardGravity * h); // 2.43 m/s
    EXPECT_NEAR(first.speed, expected, 0.15f);
}

TEST(ImpactEvents, RestingBlocksAreSilent) {
    simulation::Simulation sim;
    sim.addPlayArea();
    sim.addBlock(blocks::BlockType::Brick, materials::WoodSpecies::Beech,
                 {{0.0f, 0.5f * test::u + 0.0002f, 0.0f}, {}});
    test::runFor(sim, 1.0f);
    sim.takeImpacts();
    test::runFor(sim, 2.0f);
    EXPECT_TRUE(sim.takeImpacts().empty()) << "a block at rest must not keep clicking";
}

TEST(ImpactEvents, BlockOnBlockNamesBothBlocks) {
    simulation::Simulation sim;
    sim.addPlayArea();
    const auto lower = sim.addBlock(blocks::BlockType::Beam, materials::WoodSpecies::Beech,
                                    {{0.0f, 0.5f * test::u + 0.0002f, 0.0f}, {}});
    test::runFor(sim, 0.5f);
    sim.takeImpacts();
    const auto upper = sim.addBlock(blocks::BlockType::Cube, materials::WoodSpecies::Oak,
                                    {{0.0f, 0.2f, 0.0f}, {}});
    test::runFor(sim, 0.5f);
    bool found = false;
    for (const auto& e : sim.takeImpacts()) {
        if ((e.a.block == lower && e.b.block == upper) || (e.a.block == upper && e.b.block == lower)) {
            found = true;
        }
    }
    EXPECT_TRUE(found);
}
