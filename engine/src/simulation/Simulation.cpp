#include "woodyn/simulation/Simulation.hpp"

#include "woodyn/core/Log.hpp"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace woodyn::simulation {
namespace {

// EVA foam play mat: grippy and soft. The foam deforms under a block, so it
// strongly resists rolling and rocking (PROVISIONAL, D-46).
constexpr physics::SurfaceProperties kPlayMat{0.70f, 0.15f, 0.08f, true};

std::uint64_t pairKey(physics::BodyId a, physics::BodyId b) {
    const auto lo = std::min(a.value, b.value);
    const auto hi = std::max(a.value, b.value);
    return (static_cast<std::uint64_t>(hi) << 32) | lo;
}

} // namespace

Simulation::Simulation(const physics::PhysicsTuning& tuning)
    : m_world(tuning),
      // Allow catching up down to 20 fps before simulation time starts to lag.
      m_timestep(1.0f / tuning.stepHz,
                 std::max(1, static_cast<int>(std::ceil(tuning.stepHz / 20.0f)))) {}

physics::BodyId Simulation::addSurfaceBox(const glm::vec3& halfExtents, const glm::vec3& center,
                                          const physics::SurfaceProperties& props,
                                          audio::SurfaceKind kind) {
    physics::Transform t;
    t.position = center;
    const auto id = m_world.addStaticBox(halfExtents, t, props);
    m_surfaces[id.value] = kind;
    return id;
}

void Simulation::addFloor(float halfSize, materials::WoodSpecies surface) {
    const auto& props = materials::physicalProperties(surface);
    constexpr float kHalfThickness = 0.05f;
    addSurfaceBox({halfSize, kHalfThickness, halfSize}, {0.0f, -kHalfThickness, 0.0f},
                  {props.friction, props.restitution, props.rollingResistance}, audio::SurfaceKind::Wood);
}

void Simulation::addPlayArea(const PlayArea& area) {
    const float h = area.halfSize;
    const float t = area.wallThickness;
    const float outer = h + t;
    const auto& wood = materials::physicalProperties(materials::WoodSpecies::Beech);
    const physics::SurfaceProperties woodSurface{wood.friction, wood.restitution, wood.rollingResistance};
    physics::SurfaceProperties invisible = woodSurface; // stops blocks, ignored by picking (D-53)
    invisible.pickable = false;

    // Mat (thick slab, top at y = 0).
    addSurfaceBox({outer, t * 0.5f, outer}, {0.0f, -t * 0.5f, 0.0f}, kPlayMat,
                  audio::SurfaceKind::PlayMat);

    // Four walls: a visible wooden fence at the bottom, invisible above it.
    const float fenceHalf = area.fenceHeight * 0.5f;
    const float upperHalf = (area.wallHeight - area.fenceHeight) * 0.5f;
    for (int side = 0; side < 4; ++side) {
        const bool alongX = side < 2;
        const float sign = (side % 2 == 0) ? 1.0f : -1.0f;
        const glm::vec3 half = alongX ? glm::vec3(outer, 0.0f, t * 0.5f) : glm::vec3(t * 0.5f, 0.0f, outer);
        const glm::vec3 offset = alongX ? glm::vec3(0.0f, 0.0f, sign * (h + t * 0.5f))
                                        : glm::vec3(sign * (h + t * 0.5f), 0.0f, 0.0f);
        addSurfaceBox({half.x, fenceHalf, half.z}, offset + glm::vec3(0.0f, fenceHalf, 0.0f), woodSurface,
                      audio::SurfaceKind::Wood);
        addSurfaceBox({half.x, upperHalf, half.z},
                      offset + glm::vec3(0.0f, area.fenceHeight + upperHalf, 0.0f), invisible,
                      audio::SurfaceKind::Silent);
    }
    // Ceiling.
    addSurfaceBox({outer, t * 0.5f, outer}, {0.0f, area.wallHeight + t * 0.5f, 0.0f}, invisible,
                  audio::SurfaceKind::Silent);
    m_playArea = area;
}

std::size_t Simulation::addBlock(blocks::BlockType type, materials::WoodSpecies species,
                                 const physics::Transform& transform, materials::Stain stain,
                                 std::optional<wood::WoodCut> cut) {
    BlockInstance instance;
    instance.body = m_world.addBlock(type, species, transform);
    instance.type = type;
    instance.species = species;
    instance.stain = stain;
    instance.cut = cut ? *cut : m_wood.cut(type, species);
    instance.previous = transform;
    instance.current = transform;
    m_blocks.push_back(instance);
    m_indexByBody[instance.body.value] = m_blocks.size() - 1;
    return m_blocks.size() - 1;
}

void Simulation::setStain(std::size_t index, materials::Stain stain) {
    m_blocks.at(index).stain = stain;
}

void Simulation::removeBlock(std::size_t index) {
    if (index >= m_blocks.size()) return;
    m_world.removeBody(m_blocks[index].body);
    m_blocks.erase(m_blocks.begin() + static_cast<std::ptrdiff_t>(index));
    m_indexByBody.clear();
    for (std::size_t i = 0; i < m_blocks.size(); ++i) m_indexByBody[m_blocks[i].body.value] = i;
}

void Simulation::clearBlocks() {
    for (const auto& b : m_blocks) {
        m_world.removeBody(b.body);
    }
    m_blocks.clear();
    m_indexByBody.clear();
    m_lastImpact.clear();
}

SceneSnapshot Simulation::snapshot() const {
    SceneSnapshot s;
    for (const auto& b : m_blocks) {
        s.blocks.push_back({b.type, b.species, b.stain, b.cut, m_world.transform(b.body)});
    }
    return s;
}

void Simulation::restore(const SceneSnapshot& snapshot) {
    clearBlocks();
    for (const auto& b : snapshot.blocks) {
        addBlock(b.type, b.species, b.transform, b.stain, b.cut);
    }
    m_world.optimizeBroadPhase();
}

std::optional<std::size_t> Simulation::blockIndex(physics::BodyId body) const {
    const auto it = m_indexByBody.find(body.value);
    if (it == m_indexByBody.end()) return std::nullopt;
    return it->second;
}

StepStats Simulation::advance(float frameSeconds) {
    StepStats stats;
    if (paused) {
        return stats;
    }
    stats.steps = m_timestep.accumulate(frameSeconds);
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < stats.steps; ++i) {
        stepOnce();
    }
    const auto end = std::chrono::steady_clock::now();
    stats.physicsMilliseconds = std::chrono::duration<double, std::milli>(end - start).count();
    return stats;
}

void Simulation::stepOnce() {
    m_world.step();
    m_simulatedSeconds += static_cast<double>(m_world.stepSeconds());
    enforceBounds();
    captureTransforms();
    collectImpacts();
}

void Simulation::enforceBounds() {
    if (!m_playArea) return;
    // Safety net only: walls + CCD + throw caps should make this unreachable.
    const float limit = m_playArea->halfSize + 0.02f;
    for (const auto& b : m_blocks) {
        const glm::vec3 p = m_world.transform(b.body).position;
        const bool outside = std::abs(p.x) > limit || std::abs(p.z) > limit || p.y < -0.02f ||
                             p.y > m_playArea->wallHeight + 0.02f;
        if (!outside) continue;
        if (m_world.grabbedBody() == b.body) m_world.endGrab();
        m_world.setTransform(b.body, {{0.0f, 0.25f, 0.0f}, m_world.transform(b.body).rotation});
        m_world.setLinearVelocity(b.body, glm::vec3(0.0f));
        m_world.setAngularVelocity(b.body, glm::vec3(0.0f));
        ++m_boundsCorrections;
        log::warn("block escaped the play area at ({:.2f}, {:.2f}, {:.2f}); returned to center", p.x,
                  p.y, p.z);
    }
}

void Simulation::captureTransforms() {
    for (auto& b : m_blocks) {
        b.previous = b.current;
        b.current = m_world.transform(b.body);
    }
}

void Simulation::collectImpacts() {
    constexpr double kMinInterval = 0.03; // [s] per body pair: no machine-gun clicks
    for (const auto& e : m_world.takeImpacts()) {
        const auto key = pairKey(e.bodyA, e.bodyB);
        const auto it = m_lastImpact.find(key);
        if (it != m_lastImpact.end() && m_simulatedSeconds - it->second < kMinInterval) continue;
        m_lastImpact[key] = m_simulatedSeconds;

        auto side = [&](physics::BodyId id) {
            ImpactSide s;
            s.block = blockIndex(id);
            if (!s.block) {
                const auto surf = m_surfaces.find(id.value);
                s.surface = surf != m_surfaces.end() ? surf->second : audio::SurfaceKind::Wood;
            }
            return s;
        };
        if (m_impacts.size() < 1024) m_impacts.push_back({side(e.bodyA), side(e.bodyB), e.point, e.speed});
    }
}

std::vector<SoundImpact> Simulation::takeImpacts() {
    std::vector<SoundImpact> out;
    out.swap(m_impacts);
    return out;
}

physics::Transform Simulation::interpolated(std::size_t index) const {
    const BlockInstance& b = m_blocks.at(index);
    const float a = paused ? 1.0f : m_timestep.alpha();
    physics::Transform t;
    t.position = glm::mix(b.previous.position, b.current.position, a);
    t.rotation = glm::slerp(b.previous.rotation, b.current.rotation, a);
    return t;
}

} // namespace woodyn::simulation
