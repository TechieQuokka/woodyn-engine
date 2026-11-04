// Phase 5: the physics budget guard (D-59) and contact stability of the
// performance scenes (DESIGN.md §10 Phase 5, §11 "performance under
// concentrated collisions"). The scenes are smaller than the 1,000-block
// benchmarks so the Debug build stays fast; docs/PERFORMANCE.md has the
// full-size numbers.

#include "TestHelpers.hpp"

#include "woodyn/simulation/Scenes.hpp"

#include <gtest/gtest.h>

#include <cmath>

using namespace woodyn;
namespace scenes = simulation::scenes;

namespace {

/// Runs until every block sleeps or `maxSeconds` pass; returns the time taken.
float runUntilAsleep(simulation::Simulation& sim, float maxSeconds) {
    float t = 0.0f;
    while (t < maxSeconds) {
        test::runFor(sim, 0.1f);
        t += 0.1f;
        if (sim.world().activeBodyCount() == 0) return t;
    }
    return t;
}

/// No block is below the mat, outside the fence or numerically broken.
void expectAllInsidePlayArea(const simulation::Simulation& sim) {
    const float half = sim.playArea()->halfSize;
    for (std::size_t i = 0; i < sim.blocks().size(); ++i) {
        const glm::vec3 p = sim.world().transform(sim.blocks()[i].body).position;
        ASSERT_TRUE(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z)) << "block " << i;
        // A cube center can never be lower than half a module above the mat
        // (minus the allowed penetration slop).
        EXPECT_GT(p.y, 0.5f * test::u - 0.001f) << "block " << i << " sank into the mat";
        EXPECT_LT(std::abs(p.x), half) << "block " << i;
        EXPECT_LT(std::abs(p.z), half) << "block " << i;
    }
    EXPECT_EQ(sim.boundsCorrections(), 0u) << "the out-of-bounds safety net had to step in";
}

} // namespace

TEST(PhysicsBudget, WithoutBudgetAllDueStepsRun) {
    simulation::Simulation sim;
    sim.addPlayArea();
    scenes::buildTowerField(sim, 100);
    const auto stats = sim.advance(1.0f / 60.0f);
    EXPECT_EQ(stats.steps, 8); // 480 Hz / 60 fps
    EXPECT_EQ(stats.droppedSteps, 0);
}

TEST(PhysicsBudget, ExhaustedBudgetDropsStepsButAlwaysRunsOne) {
    simulation::Simulation sim;
    sim.addPlayArea();
    scenes::buildTowerField(sim, 100);
    sim.physicsBudgetMilliseconds = 1e-9; // any real step exceeds it
    const auto stats = sim.advance(1.0f / 60.0f);
    EXPECT_EQ(stats.steps, 1);
    EXPECT_EQ(stats.droppedSteps, 7);
    EXPECT_NEAR(sim.simulatedSeconds(), 1.0 / 480.0, 1e-9) << "dropped steps must not advance time";
    // The next frame does not try to catch up on the dropped steps.
    const auto next = sim.advance(1.0f / 60.0f);
    EXPECT_EQ(next.steps + next.droppedSteps, 8);
}

TEST(PerformanceScenes, TowerFieldStandsStillAndSleeps) {
    simulation::Simulation sim;
    sim.addPlayArea();
    const auto towers = scenes::buildTowerField(sim, 300);
    ASSERT_EQ(towers.size(), 30u);
    ASSERT_EQ(sim.blocks().size(), 300u);
    test::runFor(sim, 1.0f);
    std::vector<glm::vec3> settled;
    for (const auto& b : sim.blocks()) settled.push_back(sim.world().transform(b.body).position);
    test::runFor(sim, 2.0f);
    EXPECT_EQ(sim.world().activeBodyCount(), 0u);
    for (std::size_t i = 0; i < settled.size(); ++i) {
        EXPECT_LT(glm::length(sim.world().transform(sim.blocks()[i].body).position - settled[i]), 0.0005f)
            << "block " << i << " creeps";
    }
    expectAllInsidePlayArea(sim);
}

TEST(PerformanceScenes, CollapseComesToRestInsideThePlayArea) {
    simulation::Simulation sim;
    sim.addPlayArea();
    const auto towers = scenes::buildTowerField(sim, 300);
    test::runFor(sim, 1.0f);
    EXPECT_EQ(scenes::knockTowers(sim, towers, 0.3f), 9);
    test::runFor(sim, 0.05f);
    EXPECT_GE(sim.world().activeBodyCount(), 90u) << "the pushed towers must be falling";
    EXPECT_LT(runUntilAsleep(sim, 8.0f), 8.0f) << "collapse never came to rest";
    expectAllInsidePlayArea(sim);
}

TEST(PerformanceScenes, KnockingEverythingOverComesToRest) {
    simulation::Simulation sim;
    sim.addPlayArea();
    const auto towers = scenes::buildTowerField(sim, 300);
    test::runFor(sim, 1.0f);
    scenes::knockTowers(sim, towers, 1.0f);
    test::runFor(sim, 0.05f);
    EXPECT_EQ(sim.world().activeBodyCount(), 300u);
    EXPECT_LT(runUntilAsleep(sim, 8.0f), 8.0f) << "pile never came to rest";
    expectAllInsidePlayArea(sim);
}
