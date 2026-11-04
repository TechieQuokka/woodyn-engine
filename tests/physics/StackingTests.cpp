#include "TestHelpers.hpp"

#include "woodyn/simulation/Scenes.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace woodyn;
namespace scenes = simulation::scenes;

namespace {

std::vector<physics::Transform> snapshot(const simulation::Simulation& sim) {
    std::vector<physics::Transform> out;
    for (const auto& b : sim.blocks()) {
        out.push_back(sim.world().transform(b.body));
    }
    return out;
}

/// Maximum displacement of any block between two snapshots.
float maxDisplacement(const std::vector<physics::Transform>& a,
                      const std::vector<physics::Transform>& b) {
    float worst = 0.0f;
    for (std::size_t i = 0; i < a.size(); ++i) {
        worst = std::max(worst, glm::length(a[i].position - b[i].position));
    }
    return worst;
}

} // namespace

// DESIGN.md §11: stacked blocks must not jitter, drift or interpenetrate.
TEST(Stacking, TenCubeTowerStaysStraightAndSleeps) {
    simulation::Simulation sim;
    sim.addFloor();
    const auto tower = scenes::buildTower(sim, 10);
    test::runFor(sim, 1.0f); // let the spawn gaps close
    const auto settled = snapshot(sim);
    test::runFor(sim, 9.0f);
    const auto finalState = snapshot(sim);

    EXPECT_LT(maxDisplacement(settled, finalState), 0.0005f) << "tower creeps after settling";

    const glm::vec3 top = finalState[tower.back()].position;
    EXPECT_LT(test::horizontalDistance(top, {0.0f, 0.0f, 0.0f}), 0.001f);
    EXPECT_NEAR(top.y, 9.5f * test::u, 0.001f);

    for (std::size_t i = 1; i < tower.size(); ++i) {
        const float gap = finalState[tower[i]].position.y - finalState[tower[i - 1]].position.y;
        EXPECT_GT(gap, test::u - 0.0006f) << "penetration between cube " << i - 1 << " and " << i;
    }
    for (const auto& b : sim.blocks()) {
        EXPECT_FALSE(sim.world().isActive(b.body)) << "block still awake after 10 s";
    }
}

// D-11 regression guard: 480 Hz must keep a 15-cube (45 cm) tower standing.
TEST(Stacking, FifteenCubeTowerStandsAtDefaultRate) {
    simulation::Simulation sim;
    sim.addFloor();
    const auto tower = scenes::buildTower(sim, 15);
    test::runFor(sim, 8.0f);
    const glm::vec3 top = sim.world().transform(sim.blocks()[tower.back()].body).position;
    EXPECT_LT(test::horizontalDistance(top, {0.0f, 0.0f, 0.0f}), 0.002f);
    EXPECT_NEAR(top.y, 14.5f * test::u, 0.002f);
}

TEST(Stacking, ToyHouseStandsWithoutCollapsing) {
    simulation::Simulation sim;
    sim.addFloor();
    scenes::buildHouse(sim);
    const auto initial = snapshot(sim);
    test::runFor(sim, 5.0f);
    const auto finalState = snapshot(sim);

    EXPECT_LT(maxDisplacement(initial, finalState), 0.002f) << "house deformed";
    EXPECT_EQ(sim.world().activeBodyCount(), 0u) << "house should be asleep";
}

TEST(Stacking, EveryShapeRestsStablyOnTheFloor) {
    simulation::Simulation sim;
    sim.addFloor();
    scenes::buildShowcase(sim, {-0.3f, 0.0f, 0.0f});
    const auto initial = snapshot(sim);
    test::runFor(sim, 3.0f);
    EXPECT_LT(maxDisplacement(initial, snapshot(sim)), 0.001f);
}

TEST(Stacking, DroppedPileComesToRest) {
    simulation::Simulation sim;
    const float floorHalf = 2.0f;
    sim.addFloor(floorHalf);
    scenes::dropPile(sim, 60, 1234u);
    // A falling pile is chaotic: exact outcomes differ between compilers (D-18),
    // so only robust properties are checked.
    test::runFor(sim, 15.0f);

    int asleep = 0;
    for (const auto& b : sim.blocks()) {
        const auto p = sim.world().transform(b.body).position;
        // Round blocks may roll off the edge of the floor; only blocks still
        // above the floor area must be resting on top of it.
        if (std::abs(p.x) < floorHalf && std::abs(p.z) < floorHalf) {
            EXPECT_GT(p.y, 0.0f) << "block fell through the floor";
        }
        asleep += sim.world().isActive(b.body) ? 0 : 1;
    }
    EXPECT_GE(asleep, 51) << "at least 85% of the pile should be asleep";
}
