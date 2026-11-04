#include "TestHelpers.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

using namespace woodyn;

// DESIGN.md §11: bounce height ratio must follow h1 / h0 = e^2.
TEST(Restitution, FirstBounceHeightFollowsCoefficient) {
    simulation::Simulation sim;
    sim.addFloor();
    const float half = 0.5f * test::u;
    const float h0 = 0.3f; // drop height of the block's bottom face [m]
    const auto idx = sim.addBlock(blocks::BlockType::Cube, materials::WoodSpecies::Beech,
                                  {{0.0f, h0 + half, 0.0f}, {}});
    const auto body = sim.blocks()[idx].body;

    bool bounced = false;
    float apex = 0.0f;
    for (int i = 0; i < 240; ++i) {
        sim.stepOnce();
        const float vy = sim.world().linearVelocity(body).y;
        const float bottom = sim.world().transform(body).position.y - half;
        if (!bounced && vy > 0.0f) {
            bounced = true;
        }
        if (bounced) {
            apex = std::max(apex, bottom);
            if (vy < 0.0f) {
                break; // past the apex of the first bounce
            }
        }
    }
    ASSERT_TRUE(bounced);

    // Restitution combines with min() (D-29); block and floor are both beech.
    const float e = materials::physicalProperties(materials::WoodSpecies::Beech).restitution;
    const float measured = std::sqrt(apex / h0);
    EXPECT_NEAR(measured, e, 0.08f) << "apex " << apex << " m";
}
