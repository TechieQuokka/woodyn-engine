#pragma once

#include "woodyn/audio/ModalModels.hpp"
#include "woodyn/core/FixedTimestep.hpp"
#include "woodyn/materials/Stain.hpp"
#include "woodyn/physics/PhysicsWorld.hpp"
#include "woodyn/wood/WoodLibrary.hpp"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace woodyn::simulation {

/// A block instance tracked by the simulation layer.
struct BlockInstance {
    physics::BodyId body;
    blocks::BlockType type{};
    materials::WoodSpecies species{};
    materials::Stain stain = materials::Stain::Natural; // visual only
    wood::WoodCut cut;                                  // where it was cut from (visual only)
    physics::Transform previous; // state at the previous fixed step
    physics::Transform current;  // state at the latest fixed step
};

/// Statistics of the last `advance` call.
struct StepStats {
    int steps = 0;
    double physicsMilliseconds = 0.0; // wall time spent inside physics steps
};

/// Enclosed play area (D-28): a foam play mat surrounded by a low wooden fence,
/// with tall invisible walls and a ceiling so thrown blocks can never leave.
struct PlayArea {
    float halfSize = 0.6f;      // inner half width of the square mat [m]
    float fenceHeight = 0.045f; // visible wooden fence [m]
    float wallHeight = 1.2f;    // invisible walls and ceiling height [m]
    float wallThickness = 0.3f; // thick enough that CCD + capped throws never tunnel
};

/// One side of an impact: either a block (index into blocks()) or a static surface.
struct ImpactSide {
    std::optional<std::size_t> block;
    audio::SurfaceKind surface = audio::SurfaceKind::Wood;
};

/// An impact routed to blocks/surfaces, ready for sound synthesis.
struct SoundImpact {
    ImpactSide a;
    ImpactSide b;
    glm::vec3 point{0.0f};
    float speed = 0.0f; // [m/s]
};

/// Serializable state of all blocks, used for undo/redo.
struct SceneSnapshot {
    struct Block {
        blocks::BlockType type{};
        materials::WoodSpecies species{};
        materials::Stain stain{};
        wood::WoodCut cut;
        physics::Transform transform;
    };
    std::vector<Block> blocks;
};

/// Simulation layer (DESIGN.md §7): owns the physics world, runs it at a fixed
/// rate decoupled from rendering, and exposes interpolated transforms for the
/// renderer.
class Simulation {
public:
    explicit Simulation(const physics::PhysicsTuning& tuning = {});

    /// Adds the default floor: a large static slab whose top surface is y = 0.
    void addFloor(float halfSize = 2.0f,
                  materials::WoodSpecies surface = materials::WoodSpecies::Beech);

    /// Adds a closed play area (mat, fence, invisible walls, ceiling) and
    /// enables the out-of-bounds safety net for blocks.
    void addPlayArea(const PlayArea& area = {});
    [[nodiscard]] const std::optional<PlayArea>& playArea() const noexcept { return m_playArea; }

    /// Adds a block. Without an explicit cut, the next piece of the current
    /// plank of that species is used (every block gets its own real grain).
    std::size_t addBlock(blocks::BlockType type, materials::WoodSpecies species,
                         const physics::Transform& transform,
                         materials::Stain stain = materials::Stain::Natural,
                         std::optional<wood::WoodCut> cut = std::nullopt);

    [[nodiscard]] wood::WoodLibrary& wood() noexcept { return m_wood; }
    [[nodiscard]] const wood::WoodLibrary& wood() const noexcept { return m_wood; }

    void setStain(std::size_t index, materials::Stain stain);

    /// Removes all blocks (keeps static geometry).
    void clearBlocks();

    [[nodiscard]] SceneSnapshot snapshot() const;
    /// Replaces all blocks with the snapshot (bodies recreated at rest).
    void restore(const SceneSnapshot& snapshot);

    /// Advances real time; runs zero or more fixed steps.
    StepStats advance(float frameSeconds);

    /// Runs exactly one fixed step (used by tests and headless tools).
    void stepOnce();

    /// Transform interpolated between the last two fixed steps (for rendering).
    [[nodiscard]] physics::Transform interpolated(std::size_t index) const;

    /// Impacts since the last call (rate-limited per body pair).
    std::vector<SoundImpact> takeImpacts();

    [[nodiscard]] std::optional<std::size_t> blockIndex(physics::BodyId body) const;
    [[nodiscard]] const std::vector<BlockInstance>& blocks() const noexcept { return m_blocks; }
    [[nodiscard]] physics::PhysicsWorld& world() noexcept { return m_world; }
    [[nodiscard]] const physics::PhysicsWorld& world() const noexcept { return m_world; }
    [[nodiscard]] double simulatedSeconds() const noexcept { return m_simulatedSeconds; }
    /// How many times the out-of-bounds safety net had to rescue a block.
    [[nodiscard]] std::uint32_t boundsCorrections() const noexcept { return m_boundsCorrections; }

    bool paused = false;

private:
    void captureTransforms();
    void enforceBounds();
    void collectImpacts();
    physics::BodyId addSurfaceBox(const glm::vec3& halfExtents, const glm::vec3& center,
                                  const physics::SurfaceProperties& props, audio::SurfaceKind kind);

    physics::PhysicsWorld m_world;
    wood::WoodLibrary m_wood;
    FixedTimestep m_timestep;
    std::vector<BlockInstance> m_blocks;
    std::unordered_map<std::uint32_t, std::size_t> m_indexByBody;
    std::unordered_map<std::uint32_t, audio::SurfaceKind> m_surfaces;
    std::unordered_map<std::uint64_t, double> m_lastImpact; // pair -> time
    std::vector<SoundImpact> m_impacts;
    std::optional<PlayArea> m_playArea;
    double m_simulatedSeconds = 0.0;
    std::uint32_t m_boundsCorrections = 0;
};

} // namespace woodyn::simulation
