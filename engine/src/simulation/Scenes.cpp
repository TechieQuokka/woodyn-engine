#include "woodyn/simulation/Scenes.hpp"

#include "woodyn/core/Units.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <random>

namespace woodyn::simulation::scenes {
namespace {

using blocks::BlockType;
using materials::Stain;
constexpr float u = units::kModule;

/// Bright palette (no Natural) used to color scenes like a toy box.
constexpr std::array kPalette{Stain::Red, Stain::Yellow, Stain::Blue, Stain::Green, Stain::Orange,
                              Stain::Purple};

/// Spawns a block whose bounding-box bottom sits at `bottomY`.
std::size_t place(Simulation& sim, BlockType type, materials::WoodSpecies species, float x,
                  float bottomY, float z, Stain stain = Stain::Natural,
                  std::optional<wood::WoodCut> cut = std::nullopt,
                  const glm::quat& rotation = glm::quat(1, 0, 0, 0)) {
    const auto& def = blocks::definition(type);
    // Height of the rotated bounding box (only yaw rotations are used here).
    const float halfHeight = def.halfExtents.y;
    physics::Transform t;
    t.position = {x, bottomY + halfHeight + kSpawnGap, z};
    t.rotation = rotation;
    return sim.addBlock(type, species, t, stain, cut);
}

} // namespace

std::vector<std::size_t> buildTower(Simulation& sim, int count, float x, float z,
                                    materials::WoodSpecies species) {
    std::vector<std::size_t> out;
    // Cut the cubes one after another with vertical fibers, bottom to top: the
    // stacked tower shows one continuous grain like the beam it came from.
    sim.wood().startRun(species);
    for (int i = 0; i < count; ++i) {
        const float bottom = static_cast<float>(i) * (u + kSpawnGap);
        const auto cut = sim.wood().cut(BlockType::Cube, species, wood::FiberAxis::Y);
        out.push_back(place(sim, BlockType::Cube, species, x, bottom, z,
                            kPalette[static_cast<std::size_t>(i) % kPalette.size()], cut));
    }
    sim.world().optimizeBroadPhase();
    return out;
}

std::vector<std::size_t> buildHouse(Simulation& sim, const glm::vec3& o,
                                    materials::WoodSpecies species) {
    std::vector<std::size_t> out;
    const float layer = u + kSpawnGap;
    const float y0 = o.y;

    // Footprint: x in [-2u, 2u], z in [-3.5u, 0.5u]. Walls are 2 modules high.
    // Front wall: arch doorway (4u x 2u).
    out.push_back(place(sim, BlockType::Arch, species, o.x, y0, o.z, Stain::Red));

    for (int row = 0; row < 2; ++row) {
        const float bottom = y0 + static_cast<float>(row) * layer;
        // Side walls: cubes at x = +-1.5u, z = -1u and -2u.
        for (const float sx : {-1.5f, 1.5f}) {
            for (const float sz : {-1.0f, -2.0f}) {
                out.push_back(place(sim, BlockType::Cube, species, o.x + sx * u, bottom,
                                    o.z + sz * u, Stain::Yellow));
            }
        }
        // Back wall: two bricks side by side at z = -3u.
        for (const float bx : {-1.0f, 1.0f}) {
            out.push_back(place(sim, BlockType::Brick, species, o.x + bx * u, bottom,
                                o.z - 3.0f * u, Stain::Blue));
        }
    }

    // Ceiling: four beams spanning the width, one per depth row.
    const float ceiling = y0 + 2.0f * u + 2.0f * kSpawnGap;
    for (int row = 0; row < 4; ++row) {
        out.push_back(place(sim, BlockType::Beam, species, o.x, ceiling,
                            o.z - static_cast<float>(row) * u, Stain::Natural));
    }

    // Roof ridge: prisms along the depth, centered on the ceiling.
    const float roof = ceiling + layer;
    for (int row = 0; row < 4; ++row) {
        out.push_back(place(sim, BlockType::RoofPrism, species, o.x, roof,
                            o.z - static_cast<float>(row) * u, Stain::Green));
    }

    // Chimney: cylinder with a cone cap, standing on the ceiling beside the ridge.
    out.push_back(place(sim, BlockType::Cylinder, species, o.x + 1.5f * u, roof, o.z - 2.0f * u,
                        Stain::Orange));
    out.push_back(place(sim, BlockType::Cone, species, o.x + 1.5f * u, roof + 2.0f * u + kSpawnGap,
                        o.z - 2.0f * u, Stain::Purple));

    sim.world().optimizeBroadPhase();
    return out;
}

std::vector<std::size_t> buildShowcase(Simulation& sim, const glm::vec3& o,
                                       materials::WoodSpecies species) {
    std::vector<std::size_t> out;
    float x = o.x;
    std::size_t colorIndex = 0;
    for (const BlockType type : blocks::kAllBlockTypes) {
        const auto& def = blocks::definition(type);
        x += def.halfExtents.x;
        out.push_back(place(sim, type, species, x, o.y, o.z, kPalette[colorIndex++ % kPalette.size()]));
        x += def.halfExtents.x + u;
    }
    sim.world().optimizeBroadPhase();
    return out;
}

std::vector<std::size_t> buildGrainDemo(Simulation& sim, materials::WoodSpecies species) {
    std::vector<std::size_t> out;
    // 1) A row of bricks cut end to end from one plank, laid back together.
    sim.wood().startRun(species);
    for (int i = 0; i < 4; ++i) {
        const auto cut = sim.wood().cut(BlockType::Brick, species, wood::FiberAxis::X);
        out.push_back(place(sim, BlockType::Brick, species, -0.12f + static_cast<float>(i) * 2.0f * u, 0.0f,
                            0.12f, Stain::Natural, cut));
    }
    // 2) The next bricks from the same plank, in shuffled order: seams show.
    std::array<wood::WoodCut, 4> cuts{};
    for (auto& c : cuts) c = sim.wood().cut(BlockType::Brick, species, wood::FiberAxis::X);
    const std::array<int, 4> order{2, 0, 3, 1};
    for (int i = 0; i < 4; ++i) {
        out.push_back(place(sim, BlockType::Brick, species, -0.12f + static_cast<float>(i) * 2.0f * u, 0.0f,
                            0.04f, Stain::Natural, cuts[static_cast<std::size_t>(order[static_cast<std::size_t>(i)])]));
    }
    // 3) A continuous tower and a stained tower (grain shows through the dye).
    const auto natural = buildTower(sim, 6, -0.09f, -0.08f, species);
    for (const auto i : natural) sim.setStain(i, Stain::Natural);
    const auto stained = buildTower(sim, 6, 0.0f, -0.08f, species);
    for (std::size_t k = 0; k < stained.size(); ++k) sim.setStain(stained[k], kPalette[k % kPalette.size()]);
    out.insert(out.end(), natural.begin(), natural.end());
    out.insert(out.end(), stained.begin(), stained.end());
    // 4) One of each shape in natural beech.
    const auto shapes = buildShowcase(sim, {-0.4f, 0.0f, -0.25f}, species);
    for (const auto i : shapes) sim.setStain(i, Stain::Natural);
    out.insert(out.end(), shapes.begin(), shapes.end());
    sim.world().optimizeBroadPhase();
    return out;
}

std::vector<std::size_t> dropPile(Simulation& sim, int count, std::uint32_t seed,
                                  const glm::vec3& center, materials::WoodSpecies species) {
    std::mt19937 rng(seed);
    // Integer draws only: std::uniform_*_distribution results differ between
    // standard libraries, raw mt19937 output does not.
    auto unit = [&rng]() { return static_cast<float>(rng() % 10000u) / 10000.0f; };

    // Spawn on a 3D grid whose cell (4.5 modules) is larger than the biggest
    // block diagonal (beam: sqrt(16+1+1) ~ 4.24 modules), so no two blocks
    // ever start in penetration regardless of their random orientation.
    constexpr float kCell = 4.5f * u;
    const int perRow = std::max(1, static_cast<int>(std::ceil(std::cbrt(static_cast<float>(count)))));
    const int perLayer = perRow * perRow;
    const float rowOffset = 0.5f * static_cast<float>(perRow - 1) * kCell;

    std::vector<std::size_t> out;
    for (int i = 0; i < count; ++i) {
        const auto type = blocks::kAllBlockTypes[rng() % blocks::kAllBlockTypes.size()];
        const glm::vec3 axis = glm::normalize(glm::vec3(unit() - 0.5f, unit() - 0.5f, unit() - 0.5f) +
                                              glm::vec3(0.0f, 1e-3f, 0.0f));
        const float angle = unit() * glm::two_pi<float>();
        const int layer = i / perLayer;
        const int col = (i % perLayer) % perRow;
        const int row = (i % perLayer) / perRow;
        physics::Transform t;
        t.position = center + glm::vec3(static_cast<float>(col) * kCell - rowOffset,
                                        0.05f + (static_cast<float>(layer) + 0.5f) * kCell,
                                        static_cast<float>(row) * kCell - rowOffset);
        t.rotation = glm::angleAxis(angle, axis);
        out.push_back(sim.addBlock(type, species, t, materials::kAllStains[rng() % materials::kAllStains.size()]));
    }
    sim.world().optimizeBroadPhase();
    return out;
}

} // namespace woodyn::simulation::scenes
