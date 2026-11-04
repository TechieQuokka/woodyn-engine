#include "TestHelpers.hpp"

#include <glm/gtc/constants.hpp>
#include <gtest/gtest.h>

#include <cmath>

using namespace woodyn;

namespace {

/// Places a beech cube on a beech incline of `degrees` and returns how far it
/// moved along the slope after `seconds`.
float slideDistance(float degrees, float seconds) {
    simulation::Simulation sim;
    const auto& beech = materials::physicalProperties(materials::WoodSpecies::Beech);

    const float angle = glm::radians(degrees);
    const glm::quat tilt = glm::angleAxis(angle, glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::vec3 inclineHalf{0.4f, 0.01f, 0.2f};
    sim.world().addStaticBox(inclineHalf, {{0.0f, 0.0f, 0.0f}, tilt},
                             {beech.friction, beech.restitution});

    const glm::vec3 normal = tilt * glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::vec3 start = normal * (inclineHalf.y + 0.5f * test::u + 0.0002f);
    const auto idx = sim.addBlock(blocks::BlockType::Cube, materials::WoodSpecies::Beech,
                                  {start, tilt});
    test::runFor(sim, seconds);
    return glm::length(sim.world().transform(sim.blocks()[idx].body).position - start);
}

} // namespace

// DESIGN.md §11: the slip angle must follow tan(theta_c) = mu.
TEST(Friction, CriticalAngleMatchesCoefficient) {
    const float mu = materials::physicalProperties(materials::WoodSpecies::Beech).friction;
    const float critical = glm::degrees(std::atan(mu)); // ~24.2 deg for mu = 0.45
    ASSERT_GT(critical, 20.0f);
    ASSERT_LT(critical, 28.0f);

    EXPECT_LT(slideDistance(critical - 4.0f, 2.0f), 0.002f) << "block should hold below theta_c";
    EXPECT_GT(slideDistance(critical + 4.0f, 2.0f), 0.03f) << "block should slide above theta_c";
}

TEST(Friction, FlatSurfaceKeepsBlockAtRest) {
    EXPECT_LT(slideDistance(0.0f, 2.0f), 0.0005f);
}
