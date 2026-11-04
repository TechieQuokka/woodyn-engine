#include "TestHelpers.hpp"

#include "woodyn/simulation/Scenes.hpp"

#include <glm/gtc/quaternion.hpp>
#include <gtest/gtest.h>

using namespace woodyn;

// D-51: the placement ghost shows exactly where a block would rest.
TEST(Placement, PreviewRestsOnTheMat) {
    simulation::Simulation sim;
    sim.addPlayArea();
    const auto p = sim.world().placementPreview(blocks::BlockType::Brick, materials::WoodSpecies::Beech,
                                                glm::quat(1, 0, 0, 0), {0.1f, 0.3f, -0.1f}, 1.0f);
    ASSERT_TRUE(p.has_value());
    EXPECT_NEAR(p->position.y, 0.5f * test::u, 0.0005f);
    EXPECT_NEAR(p->position.x, 0.1f, 1e-5f);
}

TEST(Placement, PreviewStacksOnTopOfABlock) {
    simulation::Simulation sim;
    sim.addPlayArea();
    simulation::scenes::buildTower(sim, 2);
    test::runFor(sim, 0.5f);
    const auto p = sim.world().placementPreview(blocks::BlockType::Cube, materials::WoodSpecies::Beech,
                                                glm::quat(1, 0, 0, 0), {0.0f, 0.4f, 0.0f}, 1.0f);
    ASSERT_TRUE(p.has_value());
    EXPECT_NEAR(p->position.y, 2.5f * test::u, 0.001f);
}

TEST(Placement, OccupiedStartIsInvalid) {
    simulation::Simulation sim;
    sim.addPlayArea();
    sim.addBlock(blocks::BlockType::Beam, materials::WoodSpecies::Beech, {{0.0f, 0.5f * test::u, 0.0f}, {}});
    // Starting right inside the beam.
    const auto p = sim.world().placementPreview(blocks::BlockType::Cube, materials::WoodSpecies::Beech,
                                                glm::quat(1, 0, 0, 0), {0.0f, 0.5f * test::u, 0.0f}, 1.0f);
    EXPECT_FALSE(p.has_value());
}

TEST(Placement, PlacedBlockStaysWhereThePreviewSaid) {
    simulation::Simulation sim;
    sim.addPlayArea();
    const glm::quat yaw = glm::angleAxis(glm::radians(37.0f), glm::vec3(0, 1, 0));
    const auto p = sim.world().placementPreview(blocks::BlockType::Arch, materials::WoodSpecies::Oak, yaw,
                                                {0.0f, 0.3f, 0.0f}, 1.0f);
    ASSERT_TRUE(p.has_value());
    const auto i = sim.addBlock(blocks::BlockType::Arch, materials::WoodSpecies::Oak, *p);
    test::runFor(sim, 1.0f);
    EXPECT_LT(glm::length(sim.world().transform(sim.blocks()[i].body).position - p->position), 0.001f)
        << "a gently placed block must not jump or drop";
}

// D-52: removing one block keeps the rest consistent.
TEST(Placement, RemoveBlockKeepsIndicesConsistent) {
    simulation::Simulation sim;
    sim.addPlayArea();
    const auto tower = simulation::scenes::buildTower(sim, 4);
    const auto topBody = sim.blocks()[tower.back()].body;
    sim.removeBlock(1);
    ASSERT_EQ(sim.blocks().size(), 3u);
    for (std::size_t i = 0; i < sim.blocks().size(); ++i) {
        EXPECT_EQ(sim.blockIndex(sim.blocks()[i].body), i);
    }
    EXPECT_EQ(sim.blockIndex(topBody), 2u);
    test::runFor(sim, 1.0f); // the cubes above the gap fall down: still simulating fine
    EXPECT_EQ(sim.world().bodyCount(), 3u + 1u + 8u + 1u); // blocks + mat + walls (4x2) + ceiling
}
