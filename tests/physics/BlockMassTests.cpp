#include "TestHelpers.hpp"

#include <gtest/gtest.h>

using namespace woodyn;
using blocks::BlockType;

class BlockMassTest : public ::testing::TestWithParam<BlockType> {};

// Mass computed by Jolt from the collision shape must match density x volume.
TEST_P(BlockMassTest, MassMatchesDensityTimesVolume) {
    simulation::Simulation sim;
    const auto species = materials::WoodSpecies::Beech;
    const std::size_t idx = sim.addBlock(GetParam(), species, {{0.0f, 1.0f, 0.0f}, {}});
    const float mass = sim.world().mass(sim.blocks()[idx].body);
    const auto& def = blocks::definition(GetParam());
    const float expected = def.analyticVolume * materials::physicalProperties(species).density;
    EXPECT_NEAR(mass / expected, 1.0f, 0.03f) << def.name << " mass " << mass << " kg";
}

INSTANTIATE_TEST_SUITE_P(AllBlocks, BlockMassTest, ::testing::ValuesIn(blocks::kAllBlockTypes),
                         [](const ::testing::TestParamInfo<BlockType>& paramInfo) {
                             return std::string(blocks::toString(paramInfo.param));
                         });

TEST(BlockMass, BeechCubeWeighsAboutNineteenGrams) {
    simulation::Simulation sim;
    const auto idx = sim.addBlock(BlockType::Cube, materials::WoodSpecies::Beech, {});
    // 0.03^3 m^3 * 720 kg/m^3 = 19.44 g
    EXPECT_NEAR(sim.world().mass(sim.blocks()[idx].body), 0.01944f, 0.0002f);
}
