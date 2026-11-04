#pragma once

#include "woodyn/blocks/BlockCatalog.hpp"
#include "woodyn/materials/WoodSpecies.hpp"
#include "woodyn/physics/PhysicsTuning.hpp"

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace woodyn::physics {

/// Rigid transform in world space [m].
struct Transform {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
};

/// Opaque handle of a physics body.
struct BodyId {
    std::uint32_t value = 0xffffffffu;
    [[nodiscard]] bool valid() const noexcept { return value != 0xffffffffu; }
    friend bool operator==(BodyId, BodyId) = default;
};

/// Surface parameters for static geometry such as the floor or a test incline.
struct SurfaceProperties {
    float friction = 0.5f;
    float restitution = 0.5f;
    /// Rolling/rocking resistance of the surface. Soft surfaces (foam) absorb
    /// rolling and rocking far more than hard wood (D-46).
    float rollingResistance = 0.0f;
    /// Soft, compliant surface (foam): absorbs the rotational kick of impacts (D-47).
    bool soft = false;
};

/// Result of a ray cast against the physics world.
struct RayHit {
    BodyId body;
    glm::vec3 point{0.0f}; // world-space hit point [m]
    float distance = 0.0f; // along the ray [m]
};

/// A collision strong enough to be heard (D-27).
struct ImpactEvent {
    BodyId bodyA;
    BodyId bodyB;
    glm::vec3 point{0.0f};
    float speed = 0.0f; // relative approach speed along the contact normal [m/s]
};

/// Thin wrapper around Jolt Physics. Jolt types never appear in this header so
/// the rest of the engine (and the renderer) stays independent of the physics
/// back-end (DESIGN.md §7).
class PhysicsWorld {
public:
    explicit PhysicsWorld(const PhysicsTuning& tuning = {});
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    /// Adds a dynamic wooden block. Mass is derived from the species density.
    BodyId addBlock(blocks::BlockType type, materials::WoodSpecies species,
                    const Transform& transform);

    /// Adds a static box (floor, table, incline...).
    BodyId addStaticBox(const glm::vec3& halfExtents, const Transform& transform,
                        const SurfaceProperties& surface);

    void removeBody(BodyId id);
    void removeAllBodies();

    /// Advances the simulation by one fixed step of 1 / tuning.stepHz seconds.
    void step();

    /// Call after adding many bodies at once to rebuild the broad phase tree.
    void optimizeBroadPhase();

    [[nodiscard]] Transform transform(BodyId id) const;
    [[nodiscard]] glm::vec3 linearVelocity(BodyId id) const;
    [[nodiscard]] glm::vec3 angularVelocity(BodyId id) const;
    [[nodiscard]] float mass(BodyId id) const;
    [[nodiscard]] bool isActive(BodyId id) const;

    void setLinearVelocity(BodyId id, const glm::vec3& velocity);
    void setAngularVelocity(BodyId id, const glm::vec3& velocity);
    void setTransform(BodyId id, const Transform& transform);

    // --- Queries ---------------------------------------------------------------------
    /// Casts a ray; `direction` need not be normalized. Only dynamic blocks are
    /// considered when `blocksOnly` is true.
    [[nodiscard]] std::optional<RayHit> castRay(const glm::vec3& origin, const glm::vec3& direction,
                                                float maxDistance, bool blocksOnly = true) const;

    /// Where the body would come to rest if dropped straight down from its
    /// current pose (shape cast). Returns nullopt if nothing is below within maxDrop.
    [[nodiscard]] std::optional<Transform> dropPreview(BodyId id, float maxDrop = 2.0f) const;

    // --- Grab-and-drag (D-09) -----------------------------------------------------
    /// Starts holding `id` at `worldPoint` (a point on the body) with a spring "hand".
    void beginGrab(BodyId id, const glm::vec3& worldPoint);
    /// Moves the hand target. The body follows through the spring.
    void moveGrab(const glm::vec3& target);
    /// Orientation assist: rotate the held body toward `rotation`, or free swing.
    void setGrabOrientation(std::optional<glm::quat> rotation);
    /// Releases the body; it keeps its velocity (can be thrown), capped at
    /// tuning.maxThrowSpeed / maxThrowAngularSpeed.
    void endGrab();
    [[nodiscard]] std::optional<BodyId> grabbedBody() const;
    /// Current world position of the grabbed point on the body.
    [[nodiscard]] std::optional<glm::vec3> grabPoint() const;

    /// Returns and clears the impacts collected since the last call.
    std::vector<ImpactEvent> takeImpacts();

    [[nodiscard]] std::uint32_t bodyCount() const;
    [[nodiscard]] std::uint32_t activeBodyCount() const;
    [[nodiscard]] float stepSeconds() const noexcept;
    [[nodiscard]] const PhysicsTuning& tuning() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace woodyn::physics
