#include "TestHelpers.hpp"

#include "woodyn/simulation/Scenes.hpp"

#include <gtest/gtest.h>

#include <vector>

using namespace woodyn;

namespace {

std::vector<physics::Transform> runPile(int workerThreads) {
    physics::PhysicsTuning tuning;
    tuning.workerThreads = workerThreads;
    simulation::Simulation sim(tuning);
    sim.addFloor();
    simulation::scenes::dropPile(sim, 40, 42u);
    test::runFor(sim, 3.0f);
    std::vector<physics::Transform> out;
    for (const auto& b : sim.blocks()) {
        out.push_back(sim.world().transform(b.body));
    }
    return out;
}

void expectBitwiseEqual(const std::vector<physics::Transform>& a,
                        const std::vector<physics::Transform>& b) {
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        for (int k = 0; k < 3; ++k) {
            EXPECT_EQ(a[i].position[k], b[i].position[k]) << "block " << i;
        }
        for (int k = 0; k < 4; ++k) {
            EXPECT_EQ(a[i].rotation[k], b[i].rotation[k]) << "block " << i;
        }
    }
}

} // namespace

// DESIGN.md §11: repeated runs must give identical results (same build).
TEST(Determinism, RepeatedRunsAreBitwiseIdentical) {
    expectBitwiseEqual(runPile(-1), runPile(-1));
}

TEST(Determinism, ResultDoesNotDependOnThreadCount) {
    expectBitwiseEqual(runPile(1), runPile(4));
}
