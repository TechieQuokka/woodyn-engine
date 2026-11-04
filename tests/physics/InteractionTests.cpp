#include "TestHelpers.hpp"

#include "woodyn/simulation/Scenes.hpp"

#include <glm/gtc/quaternion.hpp>
#include <gtest/gtest.h>

using namespace woodyn;

namespace {

std::size_t cubeOnFloor(simulation::Simulation& sim, const glm::vec3& xz = {0.0f, 0.0f, 0.0f},
                        const glm::quat& rotation = glm::quat(1, 0, 0, 0)) {
    return sim.addBlock(blocks::BlockType::Cube, materials::WoodSpecies::Beech,
                        {{xz.x, 0.5f * test::u + 0.0002f, xz.z}, rotation});
}

float yawDegrees(const glm::quat& q) {
    const glm::vec3 x = q * glm::vec3(1.0f, 0.0f, 0.0f);
    return glm::degrees(std::atan2(-x.z, x.x));
}

} // namespace

TEST(Interaction, RayCastPicksTheBlockNotTheFloor) {
    simulation::Simulation sim;
    sim.addFloor();
    const auto idx = cubeOnFloor(sim);
    test::runFor(sim, 0.2f);
    auto& world = sim.world();

    const auto hit = world.castRay({0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 2.0f);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->body, sim.blocks()[idx].body);
    EXPECT_NEAR(hit->point.y, test::u, 0.0005f);
    EXPECT_NEAR(hit->distance, 1.0f - test::u, 0.0005f);

    EXPECT_FALSE(world.castRay({0.5f, 1.0f, 0.5f}, {0.0f, -1.0f, 0.0f}, 2.0f).has_value());
    const auto floorHit = world.castRay({0.5f, 1.0f, 0.5f}, {0.0f, -1.0f, 0.0f}, 2.0f, false);
    ASSERT_TRUE(floorHit.has_value());
    EXPECT_NEAR(floorHit->point.y, 0.0f, 0.0005f);
}

TEST(Interaction, GrabCarriesBlockAndReleaseDropsIt) {
    simulation::Simulation sim;
    sim.addFloor();
    const auto idx = cubeOnFloor(sim);
    test::runFor(sim, 0.2f);
    auto& world = sim.world();
    const auto body = sim.blocks()[idx].body;

    const glm::vec3 handle{0.0f, test::u, 0.0f}; // top face center
    world.beginGrab(body, handle);
    const glm::vec3 target = handle + glm::vec3(0.05f, 0.10f, -0.03f);
    world.moveGrab(target);
    test::runFor(sim, 1.0f);
    ASSERT_TRUE(world.grabPoint().has_value());
    EXPECT_LT(glm::length(*world.grabPoint() - target), 0.003f) << "held block lags the hand";

    world.endGrab();
    EXPECT_FALSE(world.grabbedBody().has_value());
    test::runFor(sim, 1.5f);
    EXPECT_NEAR(world.transform(body).position.y, 0.5f * test::u, 0.001f) << "block should land";
}

TEST(Interaction, OrientationAssistSnapsYaw) {
    simulation::Simulation sim;
    sim.addFloor();
    const glm::quat yaw30 = glm::angleAxis(glm::radians(30.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const auto idx = cubeOnFloor(sim, {}, yaw30);
    test::runFor(sim, 0.2f);
    auto& world = sim.world();
    const auto body = sim.blocks()[idx].body;

    world.beginGrab(body, {0.0f, test::u, 0.0f});
    world.moveGrab({0.0f, test::u + 0.05f, 0.0f});
    world.setGrabOrientation(glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    test::runFor(sim, 1.0f);
    EXPECT_NEAR(yawDegrees(world.transform(body).rotation), 0.0f, 2.0f);

    // Without assist the block keeps its yaw (no spurious spin from the hand).
    world.setGrabOrientation(std::nullopt);
    test::runFor(sim, 0.5f);
    EXPECT_NEAR(yawDegrees(world.transform(body).rotation), 0.0f, 3.0f);
}

// Natural grip (D-26 / option B): holding a block with its grab-time
// orientation as target keeps it as it was picked up, while carrying it.
TEST(Interaction, NaturalGripKeepsPickupOrientation) {
    simulation::Simulation sim;
    sim.addFloor();
    const glm::quat yaw30 = glm::angleAxis(glm::radians(30.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const auto idx = cubeOnFloor(sim, {}, yaw30);
    test::runFor(sim, 0.2f);
    auto& world = sim.world();
    const auto body = sim.blocks()[idx].body;
    world.beginGrab(body, {0.006f, test::u, 0.004f}); // slightly off-center, like a real pinch
    world.setGrabOrientation(world.transform(body).rotation);
    world.moveGrab({0.15f, 0.12f, -0.05f});
    test::runFor(sim, 1.0f);
    const glm::quat q = world.transform(body).rotation;
    EXPECT_NEAR(yawDegrees(q), 30.0f, 2.0f);
    EXPECT_GT((q * glm::vec3(0.0f, 1.0f, 0.0f)).y, 0.999f) << "block should stay upright in the hand";
}

TEST(Interaction, DropPreviewFindsTopOfTower) {
    simulation::Simulation sim;
    sim.addFloor();
    simulation::scenes::buildTower(sim, 3);
    test::runFor(sim, 0.5f);
    const auto held = sim.addBlock(blocks::BlockType::Cube, materials::WoodSpecies::Beech,
                                   {{0.0f, 0.3f, 0.0f}, {}});
    const auto preview = sim.world().dropPreview(sim.blocks()[held].body);
    ASSERT_TRUE(preview.has_value());
    EXPECT_NEAR(preview->position.y, 3.5f * test::u, 0.001f);
    EXPECT_NEAR(preview->position.x, 0.0f, 1e-5f);
}

TEST(Interaction, DropPreviewOverEmptyFloorLandsOnFloor) {
    simulation::Simulation sim;
    sim.addFloor();
    const auto held = sim.addBlock(blocks::BlockType::Arch, materials::WoodSpecies::Oak,
                                   {{0.2f, 0.5f, 0.2f}, {}});
    const auto preview = sim.world().dropPreview(sim.blocks()[held].body);
    ASSERT_TRUE(preview.has_value());
    EXPECT_NEAR(preview->position.y, test::u, 0.001f); // arch is 2 modules tall
}
