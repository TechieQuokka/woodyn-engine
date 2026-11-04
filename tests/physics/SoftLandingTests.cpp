#include "TestHelpers.hpp"

#include <glm/gtc/quaternion.hpp>
#include <gtest/gtest.h>

#include <cmath>
#include <random>

using namespace woodyn;

namespace {

/// Tilt [deg] from the nearest orientation with a face flat on the ground.
float tiltFromFlat(const glm::quat& q) {
    float best = 180.0f;
    for (const glm::vec3 a : {glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), glm::vec3(0, 0, 1)}) {
        best = std::min(best, glm::degrees(std::acos(std::min(1.0f, std::abs((q * a).y)))));
    }
    return best;
}

} // namespace

// D-47: a tilted block dropped on the foam mat lands on a face with a dull
// "thud": no rocking back up over the leading edge, at rest almost at once.
TEST(SoftLanding, TiltedBlocksSettleFlatWithoutRocking) {
    std::mt19937 rng(3);
    std::uniform_real_distribution<float> tilt(10.0f, 40.0f), yaw(0.0f, 6.28f), height(0.04f, 0.15f);
    const blocks::BlockType types[3] = {blocks::BlockType::Cube, blocks::BlockType::Brick, blocks::BlockType::Beam};
    for (int trial = 0; trial < 30; ++trial) {
        simulation::Simulation sim;
        sim.addPlayArea();
        const glm::quat q = glm::angleAxis(yaw(rng), glm::vec3(0, 1, 0)) *
                            glm::angleAxis(glm::radians(tilt(rng)), glm::normalize(glm::vec3(1, 0, 0.4f)));
        const auto body = sim.blocks()[sim.addBlock(types[trial % 3], materials::WoodSpecies::Beech,
                                                    {{0.0f, 0.07f + height(rng), 0.0f}, q})].body;
        float firstFlat = -1.0f, rebound = 0.0f, rest = -1.0f;
        for (int s = 0; s < 480 * 2; ++s) {
            sim.stepOnce();
            const auto tr = sim.world().transform(body);
            const float tf = tiltFromFlat(tr.rotation);
            const float t = static_cast<float>(s) / 480.0f;
            if (firstFlat < 0.0f && tf < 2.0f && tr.position.y < 0.08f) firstFlat = t;
            if (firstFlat >= 0.0f) {
                rebound = std::max(rebound, tf);
                const bool still = tf < 0.5f && glm::length(sim.world().angularVelocity(body)) < 0.05f;
                if (rest < 0.0f && still) rest = t;
                if (rest >= 0.0f && tf > 0.5f) rest = -1.0f;
            }
        }
        ASSERT_GE(firstFlat, 0.0f) << "trial " << trial << " never landed flat";
        EXPECT_LE(rebound, 3.0f) << "trial " << trial << " rocked back up";
        ASSERT_GE(rest, 0.0f) << "trial " << trial << " never came to rest";
        EXPECT_LE(rest - firstFlat, 0.1f) << "trial " << trial << " kept wobbling";
    }
}

// The foam absorbs impacts but must not stop ordinary rolling or tipping.
TEST(SoftLanding, FoamDoesNotFreezeSlowMotion) {
    simulation::Simulation sim;
    sim.addPlayArea();
    // A cube balanced on an edge at 30 deg, at rest: gravity must still tip it over.
    const glm::quat q = glm::angleAxis(glm::radians(30.0f), glm::vec3(0, 0, 1));
    const glm::vec3 corner = q * glm::vec3(-0.5f * test::u, -0.5f * test::u, 0.0f);
    const auto body = sim.blocks()[sim.addBlock(blocks::BlockType::Cube, materials::WoodSpecies::Beech,
                                                {{0.0f, -corner.y + 0.0001f, 0.0f}, q})].body;
    test::runFor(sim, 1.0f);
    EXPECT_LT(tiltFromFlat(sim.world().transform(body).rotation), 1.0f) << "the cube should have tipped onto a face";
}
