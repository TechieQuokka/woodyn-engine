#include "TestHelpers.hpp"

#include <glm/gtc/quaternion.hpp>
#include <gtest/gtest.h>

#include <cmath>

using namespace woodyn;

namespace {

bool inside(const simulation::Simulation& sim, const glm::vec3& p) {
    const auto& area = *sim.playArea();
    return std::abs(p.x) <= area.halfSize && std::abs(p.z) <= area.halfSize && p.y >= 0.0f &&
           p.y <= area.wallHeight;
}

} // namespace

class ThrowTest : public ::testing::TestWithParam<blocks::BlockType> {};

// D-28: whatever is thrown, at whatever speed, never leaves the play area.
// The velocities here bypass the 4 m/s throw cap on purpose (worst case).
TEST_P(ThrowTest, NothingEverEscapesThePlayArea) {
    simulation::Simulation sim;
    sim.addPlayArea();
    const std::array<glm::vec3, 6> directions{{{1, 0.2f, 0}, {-1, 0.1f, 0.3f}, {0, 0.3f, 1},
                                               {0.3f, 0.1f, -1}, {0.7f, 0.7f, 0.1f}, {0, 1, 0}}};
    for (const float speed : {4.0f, 12.0f, 30.0f}) {
        sim.clearBlocks();
        std::vector<std::size_t> ids;
        for (const auto& d : directions) {
            const auto i = sim.addBlock(GetParam(), materials::WoodSpecies::Beech,
                                        {{0.0f, 0.3f, 0.0f}, glm::angleAxis(0.7f, glm::normalize(d))});
            sim.world().setLinearVelocity(sim.blocks()[i].body, glm::normalize(d) * speed);
            sim.world().setAngularVelocity(sim.blocks()[i].body, d * 20.0f);
            ids.push_back(i);
        }
        for (int step = 0; step < 480 * 3; ++step) {
            sim.stepOnce();
        }
        for (const auto i : ids) {
            const auto p = sim.world().transform(sim.blocks()[i].body).position;
            EXPECT_TRUE(inside(sim, p)) << blocks::toString(GetParam()) << " at " << speed
                                        << " m/s ended at " << p.x << ", " << p.y << ", " << p.z;
        }
    }
    // The walls alone must hold: the safety net should never have been needed.
    EXPECT_EQ(sim.boundsCorrections(), 0u);
}

INSTANTIATE_TEST_SUITE_P(AllBlocks, ThrowTest, ::testing::ValuesIn(blocks::kAllBlockTypes),
                         [](const ::testing::TestParamInfo<blocks::BlockType>& paramInfo) {
                             return std::string(blocks::toString(paramInfo.param));
                         });

TEST(PlayArea, SafetyNetReturnsEscapedBlocks) {
    simulation::Simulation sim;
    sim.addPlayArea();
    const auto i = sim.addBlock(blocks::BlockType::Cube, materials::WoodSpecies::Beech,
                                {{0.0f, 0.3f, 0.0f}, {}});
    // Teleport outside, as a numerical failure would.
    sim.world().setTransform(sim.blocks()[i].body, {{5.0f, 0.3f, 0.0f}, {}});
    sim.stepOnce();
    EXPECT_TRUE(inside(sim, sim.world().transform(sim.blocks()[i].body).position));
    EXPECT_EQ(sim.boundsCorrections(), 1u);
}

TEST(PlayArea, ReleaseCapsThrowSpeed) {
    simulation::Simulation sim;
    sim.addPlayArea();
    const auto i = sim.addBlock(blocks::BlockType::Cube, materials::WoodSpecies::Beech,
                                {{0.0f, 0.3f, 0.0f}, {}});
    auto& world = sim.world();
    const auto body = sim.blocks()[i].body;
    world.beginGrab(body, {0.0f, 0.3f, 0.0f});
    world.setLinearVelocity(body, {25.0f, 0.0f, 0.0f});
    world.setAngularVelocity(body, {0.0f, 200.0f, 0.0f});
    world.endGrab();
    EXPECT_LE(glm::length(world.linearVelocity(body)), world.tuning().maxThrowSpeed + 1e-3f);
    EXPECT_LE(glm::length(world.angularVelocity(body)), world.tuning().maxThrowAngularSpeed + 1e-3f);
}
