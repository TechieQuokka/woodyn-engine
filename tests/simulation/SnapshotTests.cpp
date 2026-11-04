#include "TestHelpers.hpp"

#include "woodyn/simulation/Scenes.hpp"

#include <glm/gtc/quaternion.hpp>
#include <gtest/gtest.h>

using namespace woodyn;

TEST(Snapshot, RestoreRecreatesBlocksExactly) {
    simulation::Simulation sim;
    sim.addPlayArea();
    simulation::scenes::buildHouse(sim);
    test::runFor(sim, 1.0f);
    const auto saved = sim.snapshot();
    sim.clearBlocks();
    sim.addBlock(blocks::BlockType::Cube, materials::WoodSpecies::Oak, {{0.1f, 0.2f, 0.1f}, {}});
    sim.restore(saved);

    ASSERT_EQ(sim.blocks().size(), saved.blocks.size());
    for (std::size_t i = 0; i < saved.blocks.size(); ++i) {
        const auto& b = sim.blocks()[i];
        EXPECT_EQ(b.type, saved.blocks[i].type);
        EXPECT_EQ(b.stain, saved.blocks[i].stain);
        EXPECT_LT(glm::length(sim.world().transform(b.body).position - saved.blocks[i].transform.position), 1e-6f);
    }
    // The restored house still stands.
    const auto before = sim.snapshot();
    test::runFor(sim, 2.0f);
    const auto after = sim.snapshot();
    for (std::size_t i = 0; i < before.blocks.size(); ++i) {
        EXPECT_LT(glm::length(after.blocks[i].transform.position - before.blocks[i].transform.position), 0.001f);
    }
}

TEST(Snapshot, StainIsVisualOnly) {
    simulation::Simulation sim;
    const auto i = sim.addBlock(blocks::BlockType::Cube, materials::WoodSpecies::Beech, {}, materials::Stain::Red);
    const float mass = sim.world().mass(sim.blocks()[i].body);
    sim.setStain(i, materials::Stain::Blue);
    EXPECT_EQ(sim.blocks()[i].stain, materials::Stain::Blue);
    EXPECT_EQ(sim.world().mass(sim.blocks()[i].body), mass);
}

TEST(Stain, KeepsWoodVariationAndHue) {
    const glm::vec3 light = materials::stainColor(materials::Stain::Natural);
    const glm::vec3 dark = light * 0.6f; // e.g. a latewood band of the grain
    const glm::vec3 a = materials::applyStain(light, materials::Stain::Blue);
    const glm::vec3 b = materials::applyStain(dark, materials::Stain::Blue);
    EXPECT_GT(a.b, a.r) << "blue stain must look blue on beech";
    EXPECT_LT(b.b, a.b) << "grain variation stays visible through the stain";
    EXPECT_EQ(materials::applyStain(light, materials::Stain::Natural), light);
}
