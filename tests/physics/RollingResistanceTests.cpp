#include "TestHelpers.hpp"

#include <glm/gtc/quaternion.hpp>
#include <gtest/gtest.h>

using namespace woodyn;

namespace {

struct RollResult {
    float distance;
    float finalSpeed;
};

/// Rolls a beech cylinder (axis along X) at v0 along +Z and measures it.
RollResult roll(float v0, float seconds, bool rollingResistance) {
    physics::PhysicsTuning tuning;
    tuning.rollingResistance = rollingResistance;
    simulation::Simulation sim(tuning);
    sim.addFloor(6.0f);
    const auto& def = blocks::definition(blocks::BlockType::Cylinder);
    const float r = def.cylinderRadius;
    const glm::quat lying = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.0f, 0.0f, 1.0f));
    const auto idx = sim.addBlock(blocks::BlockType::Cylinder, materials::WoodSpecies::Beech,
                                  {{0.0f, r, -2.0f}, lying});
    const auto body = sim.blocks()[idx].body;
    sim.world().setLinearVelocity(body, {0.0f, 0.0f, v0});
    sim.world().setAngularVelocity(body, {v0 / r, 0.0f, 0.0f}); // rolling without slipping
    const glm::vec3 start = sim.world().transform(body).position;
    test::runFor(sim, seconds);
    const glm::vec3 end = sim.world().transform(body).position;
    return {test::horizontalDistance(start, end), glm::length(sim.world().linearVelocity(body))};
}

} // namespace

// A rolling solid cylinder decelerates at a = (2/3) c_rr g, so it stops after
// d = v0^2 / (2a).
TEST(RollingResistance, CylinderStopsAtPredictedDistance) {
    const float v0 = 0.3f;
    const float c = materials::physicalProperties(materials::WoodSpecies::Beech).rollingResistance;
    const float a = (2.0f / 3.0f) * c * units::kStandardGravity;
    const float expected = v0 * v0 / (2.0f * a); // ~1.15 m for c = 0.006
    const auto result = roll(v0, 12.0f, true);
    EXPECT_NEAR(result.distance / expected, 1.0f, 0.25f) << "rolled " << result.distance << " m";
    EXPECT_LT(result.finalSpeed, 0.001f) << "cylinder should have stopped";
}

TEST(RollingResistance, WithoutItCylinderKeepsRolling) {
    const auto result = roll(0.3f, 6.0f, false);
    EXPECT_GT(result.distance, 1.6f);
    EXPECT_GT(result.finalSpeed, 0.25f);
}
