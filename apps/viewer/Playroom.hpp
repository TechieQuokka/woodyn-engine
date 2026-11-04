#pragma once

#include "woodyn/geometry/RoundedMesh.hpp"
#include "woodyn/simulation/Simulation.hpp"

#include <glm/vec3.hpp>

#include <vector>

namespace woodyn::viewer {

/// Visual theme of the room around the play area (D-30). Colors are sRGB.
struct Skin {
    glm::vec3 wallTop{0.99f, 0.93f, 0.84f};      // warm cream
    glm::vec3 wallBottom{0.98f, 0.84f, 0.78f};   // soft peach
    glm::vec3 baseboard{0.95f, 0.95f, 0.93f};
    std::vector<glm::vec3> dots{{0.62f, 0.82f, 0.96f}, {1.00f, 0.80f, 0.86f},
                                {0.80f, 0.92f, 0.70f}, {1.00f, 0.90f, 0.55f}};
    glm::vec3 floorA{0.80f, 0.64f, 0.46f};       // light oak floorboards
    glm::vec3 floorB{0.74f, 0.58f, 0.41f};
    std::vector<glm::vec3> matTiles{{0.99f, 0.66f, 0.72f},  // pink
                                    {0.60f, 0.86f, 0.80f},  // mint
                                    {0.62f, 0.78f, 0.98f},  // sky blue
                                    {1.00f, 0.88f, 0.52f}}; // lemon
    glm::vec3 fence{0.93f, 0.78f, 0.58f};        // natural lacquered beech
    glm::vec3 background{0.99f, 0.93f, 0.84f};
};

/// Pre-built, pre-colored room geometry (linear RGB per vertex).
struct RoomTriangle {
    glm::vec3 p[3];
    glm::vec3 color[3]; // linear, already lit
};

struct Room {
    std::vector<RoomTriangle> backdrop; // walls, floorboards, mat: drawn first, in order
    geometry::RenderMesh fence;         // rounded rails, depth-sorted with the blocks
    glm::vec3 fenceColorLinear{1.0f};
    float roomHalf = 2.4f;
};

/// Builds the playroom around `area` (or a default-size room without fence).
Room buildPlayroom(const Skin& skin, const simulation::PlayArea* area);

} // namespace woodyn::viewer
