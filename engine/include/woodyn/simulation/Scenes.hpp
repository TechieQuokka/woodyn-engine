#pragma once

#include "woodyn/simulation/Simulation.hpp"

#include <cstdint>
#include <vector>

namespace woodyn::simulation::scenes {

/// Vertical gap left between stacked blocks at spawn time [m], so that bodies
/// never start in penetration (they settle by ~0.2 mm on the first steps).
inline constexpr float kSpawnGap = 0.0002f;

/// Stacks `count` cubes at (x, z). Returns the indices of the created blocks.
std::vector<std::size_t> buildTower(Simulation& sim, int count, float x = 0.0f, float z = 0.0f,
                                    materials::WoodSpecies species = materials::WoodSpecies::Beech);

/// Builds a small toy house (arch door, cube/brick walls, beam ceiling, prism
/// roof and a chimney) centered near `origin`. Uses every block shape except
/// the half cylinder and pyramid.
std::vector<std::size_t> buildHouse(Simulation& sim, const glm::vec3& origin = {0.0f, 0.0f, 0.0f},
                                    materials::WoodSpecies species = materials::WoodSpecies::Beech);

/// Places one block of every type in a row along +X, resting on the floor.
std::vector<std::size_t> buildShowcase(Simulation& sim, const glm::vec3& origin = {0.0f, 0.0f, 0.0f},
                                       materials::WoodSpecies species = materials::WoodSpecies::Beech);

/// Shows grain continuity (DESIGN.md §5): bricks cut end to end and laid back
/// together vs. shuffled, a continuous natural tower, a stained tower, shapes.
std::vector<std::size_t> buildGrainDemo(Simulation& sim,
                                        materials::WoodSpecies species = materials::WoodSpecies::Beech);

/// Drops `count` random blocks (deterministic for a given seed) from above.
std::vector<std::size_t> dropPile(Simulation& sim, int count, std::uint32_t seed,
                                  const glm::vec3& center = {0.0f, 0.0f, 0.0f},
                                  materials::WoodSpecies species = materials::WoodSpecies::Beech);

/// Performance scene (Phase 5, D-10): a square grid of cube towers standing
/// on the play mat. `count` blocks are split into towers of `towerHeight`
/// cubes (the last tower may be shorter), `spacing` apart (reduced so the
/// field fits on the play mat, if there is one). Returns the block
/// indices of every tower, bottom to top, in row-major order.
std::vector<std::vector<std::size_t>> buildTowerField(Simulation& sim, int count, int towerHeight = 10,
                                                      float spacing = 0.1f);

/// Tips the first `fraction` of the towers (row-major) over towards +X, like a
/// child pushing them: each tower starts rotating rigidly about its base so its
/// top moves at `speed` [m/s]. Returns how many towers
/// were pushed.
int knockTowers(Simulation& sim, const std::vector<std::vector<std::size_t>>& towers, float fraction,
                float speed = 0.6f);

} // namespace woodyn::simulation::scenes
