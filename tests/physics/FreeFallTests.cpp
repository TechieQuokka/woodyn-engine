#include "TestHelpers.hpp"

#include <gtest/gtest.h>

using namespace woodyn;

// DESIGN.md §11: compare free-fall against the analytic solution.
TEST(FreeFall, MatchesAnalyticSolutionWithinIntegratorError) {
    simulation::Simulation sim; // no floor
    const glm::vec3 start{0.0f, 10.0f, 0.0f};
    const auto idx = sim.addBlock(blocks::BlockType::Cube, materials::WoodSpecies::Beech,
                                  {start, {}});
    const float t = 0.5f;
    test::runFor(sim, t);

    const auto body = sim.blocks()[idx].body;
    const float g = units::kStandardGravity;
    const float drop = start.y - sim.world().transform(body).position.y;
    const float exactDrop = 0.5f * g * t * t; // 1.2258 m

    // Jolt integrates with symplectic Euler: velocity is exact, position
    // overshoots by ~1/N (N = 60 steps -> +1.7%).
    EXPECT_NEAR(drop / exactDrop, 1.0f, 0.025f);
    EXPECT_GT(drop, exactDrop); // overshoot direction is known
    EXPECT_NEAR(-sim.world().linearVelocity(body).y, g * t, 1e-3f * g * t);
}

TEST(FreeFall, NoHorizontalDriftOrSpin) {
    simulation::Simulation sim;
    const auto idx = sim.addBlock(blocks::BlockType::Arch, materials::WoodSpecies::Oak,
                                  {{0.0f, 5.0f, 0.0f}, {}});
    test::runFor(sim, 0.5f);
    const auto body = sim.blocks()[idx].body;
    const auto tr = sim.world().transform(body);
    EXPECT_NEAR(tr.position.x, 0.0f, 1e-5f);
    EXPECT_NEAR(tr.position.z, 0.0f, 1e-5f);
    EXPECT_LT(glm::length(sim.world().angularVelocity(body)), 1e-4f);
}
