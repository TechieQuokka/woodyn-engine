#include "woodyn/physics/PhysicsWorld.hpp"

#include "physics/ShapeFactory.hpp"
#include "woodyn/core/Log.hpp"

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h>
#include <Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterTable.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterTable.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace woodyn::physics {
namespace {

namespace Layers {
constexpr JPH::ObjectLayer kStatic = 0;
constexpr JPH::ObjectLayer kMoving = 1;
constexpr JPH::uint kCount = 2;
} // namespace Layers

namespace BroadPhaseLayers {
constexpr JPH::BroadPhaseLayer kStatic{0};
constexpr JPH::BroadPhaseLayer kMoving{1};
constexpr JPH::uint kCount = 2;
} // namespace BroadPhaseLayers

void joltTrace(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buffer[1024];
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    log::debug("[Jolt] {}", buffer);
}

/// Jolt global state (allocator, factory, type registry) is process-wide.
void ensureJoltInitialized() {
    static std::once_flag flag;
    std::call_once(flag, [] {
        JPH::RegisterDefaultAllocator();
        JPH::Trace = joltTrace;
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
        log::info("Jolt Physics initialized");
    });
}

JPH::Vec3 toJolt(const glm::vec3& v) { return {v.x, v.y, v.z}; }
JPH::Quat toJolt(const glm::quat& q) { return {q.x, q.y, q.z, q.w}; }
glm::vec3 fromJolt(JPH::Vec3Arg v) { return {v.GetX(), v.GetY(), v.GetZ()}; }
glm::quat fromJolt(JPH::QuatArg q) { return {q.GetW(), q.GetX(), q.GetY(), q.GetZ()}; }

JPH::BodyID toJolt(BodyId id) { return JPH::BodyID(id.value); }

/// Remembers which dynamic bodies touched anything during the last step.
/// Jolt calls the listener from worker threads, hence the atomics.
class ContactTracker final : public JPH::ContactListener {
public:
    struct RawImpact {
        JPH::BodyID a;
        JPH::BodyID b;
        JPH::Vec3 point;
        float speed; // Jolt units
    };

    explicit ContactTracker(std::uint32_t maxBodies) : m_flags(maxBodies) {}

    void configure(float minSpeed, float persistedMinSpeed) {
        m_minSpeed = minSpeed;
        m_persistedMinSpeed = persistedMinSpeed;
    }

    void OnContactAdded(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& m,
                        JPH::ContactSettings&) override {
        mark(a);
        mark(b);
        recordImpact(a, b, m, m_minSpeed);
    }
    void OnContactPersisted(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& m,
                            JPH::ContactSettings&) override {
        mark(a);
        mark(b);
        recordImpact(a, b, m, m_persistedMinSpeed);
    }

    std::vector<RawImpact> takeImpacts() {
        std::lock_guard lock(m_impactMutex);
        std::vector<RawImpact> out;
        out.swap(m_impacts);
        return out;
    }

    /// Returns whether the body touched something, and clears the flag.
    bool consume(JPH::BodyID id) {
        return m_flags[id.GetIndex()].exchange(0, std::memory_order_relaxed) != 0;
    }

private:
    void recordImpact(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& m,
                      float threshold) {
        if (m.mRelativeContactPointsOn1.empty()) return;
        const JPH::RVec3 p = m.GetWorldSpaceContactPointOn1(0);
        // Normal points from body 1 to body 2: approaching when (v1 - v2) . n > 0.
        const float approach = (a.GetPointVelocity(p) - b.GetPointVelocity(p)).Dot(m.mWorldSpaceNormal);
        if (approach < threshold) return;
        std::lock_guard lock(m_impactMutex);
        if (m_impacts.size() < 4096) m_impacts.push_back({a.GetID(), b.GetID(), p, approach});
    }

    void mark(const JPH::Body& body) {
        if (body.IsDynamic()) {
            m_flags[body.GetID().GetIndex()].store(1, std::memory_order_relaxed);
        }
    }
    std::vector<std::atomic<std::uint8_t>> m_flags;
    float m_minSpeed = 0.03f;
    float m_persistedMinSpeed = 0.3f;
    std::mutex m_impactMutex;
    std::vector<RawImpact> m_impacts;
};
BodyId fromJolt(JPH::BodyID id) { return BodyId{id.GetIndexAndSequenceNumber()}; }

} // namespace

struct PhysicsWorld::Impl {
    explicit Impl(const PhysicsTuning& t)
        : tuning(t),
          objectPairFilter(Layers::kCount),
          broadPhaseLayers(Layers::kCount, BroadPhaseLayers::kCount),
          tempAllocator(32 * 1024 * 1024),
          shapes(t),
          scale(t.lengthScale),
          invScale(1.0f / t.lengthScale),
          contacts(t.maxBodies) {
        objectPairFilter.EnableCollision(Layers::kMoving, Layers::kStatic);
        objectPairFilter.EnableCollision(Layers::kMoving, Layers::kMoving);
        broadPhaseLayers.MapObjectToBroadPhaseLayer(Layers::kStatic, BroadPhaseLayers::kStatic);
        broadPhaseLayers.MapObjectToBroadPhaseLayer(Layers::kMoving, BroadPhaseLayers::kMoving);
        // The table snapshots the layer setup at construction: build it last.
        objectVsBroadPhase = std::make_unique<JPH::ObjectVsBroadPhaseLayerFilterTable>(
            broadPhaseLayers, BroadPhaseLayers::kCount, objectPairFilter, Layers::kCount);

        int threads = t.workerThreads;
        if (threads <= 0) {
            threads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1);
        }
        jobSystem = std::make_unique<JPH::JobSystemThreadPool>(
            JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, threads);

        system.Init(t.maxBodies, 0, t.maxBodyPairs, t.maxContactConstraints, broadPhaseLayers,
                    *objectVsBroadPhase, objectPairFilter);
        system.SetGravity(JPH::Vec3(0.0f, -units::kStandardGravity * scale, 0.0f));
        system.SetContactListener(&contacts);
        // The softer surface dominates a collision (foam mat absorbs a wooden
        // block's bounce); wood on wood keeps its own restitution (D-29).
        system.SetCombineRestitution([](const JPH::Body& a, const JPH::SubShapeID&, const JPH::Body& b,
                                        const JPH::SubShapeID&) {
            return std::min(a.GetRestitution(), b.GetRestitution());
        });
        contacts.configure(t.impactMinSpeed * scale, t.impactPersistedMinSpeed * scale);

        JPH::PhysicsSettings s;
        s.mBaumgarte = t.baumgarte;
        const auto sq = [](float x) { return x * x; };
        s.mSpeculativeContactDistance = t.speculativeContactDistance * scale;
        s.mPenetrationSlop = t.penetrationSlop * scale;
        s.mMaxPenetrationDistance = t.maxPenetrationDistance * scale;
        s.mManifoldTolerance = t.manifoldTolerance * scale;
        s.mBodyPairCacheMaxDeltaPositionSq = sq(t.bodyPairCacheMaxDeltaPosition * scale);
        s.mBodyPairCacheCosMaxDeltaRotationDiv2 =
            std::cos(0.5f * t.bodyPairCacheMaxDeltaRotationDegrees * JPH::JPH_PI / 180.0f);
        s.mContactPointPreserveLambdaMaxDistSq = sq(t.contactPointPreserveLambdaMaxDist * scale);
        s.mMinVelocityForRestitution = t.minVelocityForRestitution * scale;
        s.mNumVelocitySteps = t.velocitySteps;
        s.mNumPositionSteps = t.positionSteps;
        s.mTimeBeforeSleep = t.timeBeforeSleep;
        s.mPointVelocitySleepThreshold = t.pointVelocitySleepThreshold * scale;
        s.mDeterministicSimulation = true;
        s.mConstraintWarmStart = t.constraintWarmStart;
        s.mUseBodyPairContactCache = t.useBodyPairContactCache;
        s.mUseManifoldReduction = t.useManifoldReduction;
        s.mUseLargeIslandSplitter = t.useLargeIslandSplitter;
        system.SetPhysicsSettings(s);

        log::info("PhysicsWorld: {} Hz x {} collision steps, {} worker threads, "
                  "convex radius {} m, length scale {}",
                  t.stepHz, t.collisionStepsPerUpdate, threads, t.convexRadius, scale);
    }

    PhysicsTuning tuning;
    JPH::ObjectLayerPairFilterTable objectPairFilter;
    JPH::BroadPhaseLayerInterfaceTable broadPhaseLayers;
    std::unique_ptr<JPH::ObjectVsBroadPhaseLayerFilterTable> objectVsBroadPhase;
    JPH::TempAllocatorImpl tempAllocator;
    std::unique_ptr<JPH::JobSystemThreadPool> jobSystem;
    JPH::PhysicsSystem system;
    detail::ShapeFactory shapes;
    float scale;    // Jolt units per meter
    float invScale; // meters per Jolt unit
    ContactTracker contacts;

    struct Grab {
        JPH::BodyID body;
        JPH::Vec3 localPoint;   // grab point relative to the body origin, body space
        JPH::Vec3 target;       // hand position (Jolt units)
        std::optional<JPH::Quat> orientation;
    };
    std::optional<Grab> grab;

    float massOf(const JPH::Body& body) const {
        const float inv = body.GetMotionProperties()->GetInverseMass();
        return inv > 0.0f ? 1.0f / inv : 0.0f;
    }

    /// Spring-damper "hand" applied before each step (D-20).
    void applyGrab(float dt) {
        if (!grab) return;
        auto& bi = bodies();
        const JPH::BodyID id = grab->body;
        JPH::RVec3 pos;
        JPH::Quat rot;
        bi.GetPositionAndRotation(id, pos, rot);
        const JPH::Vec3 point = pos + rot * grab->localPoint;
        const JPH::Vec3 velocity = bi.GetPointVelocity(id, point);

        float m = 0.0f;
        {
            JPH::BodyLockRead lock(system.GetBodyLockInterface(), id);
            if (!lock.Succeeded() || !lock.GetBody().IsDynamic()) {
                grab.reset();
                return;
            }
            m = massOf(lock.GetBody());
        }
        const float w = 2.0f * JPH::JPH_PI * tuning.grabFrequencyHz;
        JPH::Vec3 force = (grab->target - point) * (m * w * w) -
                          velocity * (2.0f * tuning.grabDampingRatio * m * w);
        if (tuning.grabCompensateGravity) {
            force += JPH::Vec3(0.0f, m * units::kStandardGravity * scale, 0.0f);
        }
        const float maxForce = m * tuning.grabMaxAcceleration * scale;
        if (force.LengthSq() > maxForce * maxForce) {
            force = force.Normalized() * maxForce;
        }
        // With an orientation target the grip holds the pose firmly, so the
        // pull goes through the center of mass (no torque from an off-center
        // pinch). In free-swing mode it acts at the grabbed point.
        if (grab->orientation) {
            bi.AddImpulse(id, force * dt);
        } else {
            bi.AddImpulse(id, force * dt, point);
        }
        bi.ActivateBody(id);

        if (grab->orientation) {
            JPH::Quat dq = *grab->orientation * rot.Conjugated();
            if (dq.GetW() < 0.0f) dq = -dq;
            JPH::Vec3 axis;
            float angle = 0.0f;
            dq.GetAxisAngle(axis, angle);
            const JPH::Vec3 desired = angle > 1e-6f
                ? axis * (angle * 2.0f * JPH::JPH_PI * tuning.grabRotationFrequencyHz)
                : JPH::Vec3::sZero();
            const JPH::Vec3 current = bi.GetAngularVelocity(id);
            const float blend = std::min(1.0f, tuning.grabRotationResponse * dt);
            bi.SetAngularVelocity(id, current + (desired - current) * blend);
        } else {
            const float keep = std::max(0.0f, 1.0f - tuning.grabHeldAngularDamping * dt);
            bi.SetAngularVelocity(id, bi.GetAngularVelocity(id) * keep);
        }
    }

    /// Rolling resistance (D-21): torque c_rr * m * g * r opposing the spin of
    /// every dynamic body that is in contact with something.
    void applyRollingResistance(float dt) {
        if (!tuning.rollingResistance) return;
        JPH::BodyIDVector active;
        system.GetActiveBodies(JPH::EBodyType::RigidBody, active);
        const float g = units::kStandardGravity * scale;
        for (const JPH::BodyID id : active) {
            if (!contacts.consume(id) || (grab && grab->body == id)) continue;
            JPH::BodyLockWrite lock(system.GetBodyLockInterface(), id);
            if (!lock.Succeeded()) continue;
            JPH::Body& body = lock.GetBody();
            if (!body.IsDynamic()) continue;
            const float lever = std::bit_cast<float>(static_cast<std::uint32_t>(body.GetUserData()));
            const JPH::Vec3 omega = body.GetAngularVelocity();
            const float speed = omega.Length();
            if (lever <= 0.0f || speed < 1e-6f) continue;
            const JPH::Vec3 dL = omega * (-lever * massOf(body) * g * dt / speed);
            const JPH::Vec3 dw =
                body.GetMotionProperties()->MultiplyWorldSpaceInverseInertiaByVector(body.GetRotation(), dL);
            const float slowdown = -dw.Dot(omega) / speed;
            body.SetAngularVelocity(slowdown >= speed ? JPH::Vec3::sZero() : omega + dw);
        }
    }

    // SI <-> Jolt conversions (lengths and linear velocities scale, angles do not).
    JPH::Vec3 len(const glm::vec3& v) const { return toJolt(v * scale); }
    glm::vec3 len(JPH::Vec3Arg v) const { return fromJolt(v) * invScale; }

    JPH::BodyInterface& bodies() { return system.GetBodyInterface(); }
    const JPH::BodyInterface& bodies() const { return system.GetBodyInterface(); }
};

PhysicsWorld::PhysicsWorld(const PhysicsTuning& tuning) {
    ensureJoltInitialized();
    m_impl = std::make_unique<Impl>(tuning);
}

PhysicsWorld::~PhysicsWorld() {
    if (m_impl) {
        removeAllBodies();
    }
}

BodyId PhysicsWorld::addBlock(blocks::BlockType type, materials::WoodSpecies species,
                              const Transform& transform) {
    const auto& props = materials::physicalProperties(species);
    const auto shape = m_impl->shapes.blockShape(type, props.density);

    JPH::BodyCreationSettings settings(shape, m_impl->len(transform.position),
                                       toJolt(transform.rotation), JPH::EMotionType::Dynamic,
                                       Layers::kMoving);
    settings.mFriction = props.friction;
    settings.mRestitution = props.restitution;
    settings.mLinearDamping = m_impl->tuning.linearDamping;
    settings.mAngularDamping = m_impl->tuning.angularDamping;
    settings.mMotionQuality = m_impl->tuning.continuousCollision ? JPH::EMotionQuality::LinearCast
                                                                 : JPH::EMotionQuality::Discrete;
    // Rolling lever c_rr * r (Jolt units), r = the shape's inner radius.
    const float lever = props.rollingResistance * shape->GetInnerRadius();
    settings.mUserData = std::bit_cast<std::uint32_t>(lever);

    const JPH::BodyID id = m_impl->bodies().CreateAndAddBody(settings, JPH::EActivation::Activate);
    if (id.IsInvalid()) {
        throw std::runtime_error("PhysicsWorld: body limit reached");
    }
    return fromJolt(id);
}

BodyId PhysicsWorld::addStaticBox(const glm::vec3& halfExtents, const Transform& transform,
                                  const SurfaceProperties& surface) {
    const float radius = std::min({m_impl->tuning.convexRadius, halfExtents.x, halfExtents.y,
                                   halfExtents.z}) * m_impl->scale;
    JPH::BodyCreationSettings settings(new JPH::BoxShape(m_impl->len(halfExtents), radius),
                                       m_impl->len(transform.position), toJolt(transform.rotation),
                                       JPH::EMotionType::Static, Layers::kStatic);
    settings.mFriction = surface.friction;
    settings.mRestitution = surface.restitution;
    const JPH::BodyID id =
        m_impl->bodies().CreateAndAddBody(settings, JPH::EActivation::DontActivate);
    if (id.IsInvalid()) {
        throw std::runtime_error("PhysicsWorld: body limit reached");
    }
    return fromJolt(id);
}

void PhysicsWorld::removeBody(BodyId id) {
    if (m_impl->grab && m_impl->grab->body == toJolt(id)) {
        m_impl->grab.reset();
    }
    auto& bi = m_impl->bodies();
    const JPH::BodyID jid = toJolt(id);
    bi.RemoveBody(jid);
    bi.DestroyBody(jid);
}

void PhysicsWorld::removeAllBodies() {
    m_impl->grab.reset();
    JPH::BodyIDVector ids;
    m_impl->system.GetBodies(ids);
    if (ids.empty()) {
        return;
    }
    auto& bi = m_impl->bodies();
    bi.RemoveBodies(ids.data(), static_cast<int>(ids.size()));
    bi.DestroyBodies(ids.data(), static_cast<int>(ids.size()));
}

void PhysicsWorld::step() {
    m_impl->applyGrab(stepSeconds());
    const JPH::EPhysicsUpdateError err =
        m_impl->system.Update(stepSeconds(), m_impl->tuning.collisionStepsPerUpdate,
                              &m_impl->tempAllocator, m_impl->jobSystem.get());
    if (err != JPH::EPhysicsUpdateError::None) {
        log::warn("Jolt update reported error flags: {}", static_cast<unsigned>(err));
    }
    m_impl->applyRollingResistance(stepSeconds());
}

void PhysicsWorld::optimizeBroadPhase() { m_impl->system.OptimizeBroadPhase(); }

Transform PhysicsWorld::transform(BodyId id) const {
    JPH::RVec3 pos;
    JPH::Quat rot;
    m_impl->bodies().GetPositionAndRotation(toJolt(id), pos, rot);
    return {m_impl->len(pos), fromJolt(rot)};
}

glm::vec3 PhysicsWorld::linearVelocity(BodyId id) const {
    return m_impl->len(m_impl->bodies().GetLinearVelocity(toJolt(id)));
}

glm::vec3 PhysicsWorld::angularVelocity(BodyId id) const {
    return fromJolt(m_impl->bodies().GetAngularVelocity(toJolt(id)));
}

float PhysicsWorld::mass(BodyId id) const {
    JPH::BodyLockRead lock(m_impl->system.GetBodyLockInterface(), toJolt(id));
    if (!lock.Succeeded()) {
        return 0.0f;
    }
    const JPH::Body& body = lock.GetBody();
    if (!body.IsDynamic()) {
        return 0.0f;
    }
    const float inv = body.GetMotionProperties()->GetInverseMass();
    return inv > 0.0f ? 1.0f / inv : 0.0f;
}

bool PhysicsWorld::isActive(BodyId id) const { return m_impl->bodies().IsActive(toJolt(id)); }

void PhysicsWorld::setLinearVelocity(BodyId id, const glm::vec3& velocity) {
    m_impl->bodies().SetLinearVelocity(toJolt(id), m_impl->len(velocity));
}

void PhysicsWorld::setAngularVelocity(BodyId id, const glm::vec3& velocity) {
    m_impl->bodies().SetAngularVelocity(toJolt(id), toJolt(velocity));
}

void PhysicsWorld::setTransform(BodyId id, const Transform& transform) {
    m_impl->bodies().SetPositionAndRotation(toJolt(id), m_impl->len(transform.position),
                                            toJolt(transform.rotation),
                                            JPH::EActivation::Activate);
}

std::optional<RayHit> PhysicsWorld::castRay(const glm::vec3& origin, const glm::vec3& direction,
                                            float maxDistance, bool blocksOnly) const {
    const float len = glm::length(direction);
    if (len < 1e-9f || maxDistance <= 0.0f) return std::nullopt;
    const JPH::RRayCast ray{m_impl->len(origin), m_impl->len(direction * (maxDistance / len))};
    JPH::RayCastResult hit;
    const auto& query = m_impl->system.GetNarrowPhaseQuery();
    bool found = false;
    if (blocksOnly) {
        const JPH::SpecifiedBroadPhaseLayerFilter bpFilter(BroadPhaseLayers::kMoving);
        const JPH::SpecifiedObjectLayerFilter layerFilter(Layers::kMoving);
        found = query.CastRay(ray, hit, bpFilter, layerFilter);
    } else {
        found = query.CastRay(ray, hit);
    }
    if (!found) return std::nullopt;
    return RayHit{fromJolt(hit.mBodyID), m_impl->len(ray.GetPointOnRay(hit.mFraction)),
                  hit.mFraction * maxDistance};
}

std::optional<Transform> PhysicsWorld::dropPreview(BodyId id, float maxDrop) const {
    JPH::RefConst<JPH::Shape> shape;
    JPH::RMat44 world;
    JPH::Quat rotation;
    {
        JPH::BodyLockRead lock(m_impl->system.GetBodyLockInterface(), toJolt(id));
        if (!lock.Succeeded()) return std::nullopt;
        shape = lock.GetBody().GetShape();
        world = lock.GetBody().GetWorldTransform();
        rotation = lock.GetBody().GetRotation();
    }
    const JPH::Vec3 direction(0.0f, -maxDrop * m_impl->scale, 0.0f);
    const JPH::RShapeCast cast =
        JPH::RShapeCast::sFromWorldTransform(shape, JPH::Vec3::sReplicate(1.0f), world, direction);
    JPH::ShapeCastSettings settings;
    JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
    const JPH::IgnoreSingleBodyFilter ignoreSelf(toJolt(id));
    m_impl->system.GetNarrowPhaseQuery().CastShape(cast, settings, JPH::RVec3::sZero(), collector,
                                                   {}, {}, ignoreSelf);
    if (!collector.HadHit()) return std::nullopt;
    const JPH::Vec3 origin = world.GetTranslation();
    return Transform{m_impl->len(origin + direction * collector.mHit.mFraction), fromJolt(rotation)};
}

void PhysicsWorld::beginGrab(BodyId id, const glm::vec3& worldPoint) {
    JPH::RVec3 pos;
    JPH::Quat rot;
    m_impl->bodies().GetPositionAndRotation(toJolt(id), pos, rot);
    const JPH::Vec3 p = m_impl->len(worldPoint);
    m_impl->grab = Impl::Grab{toJolt(id), rot.Conjugated() * (p - pos), p, std::nullopt};
    m_impl->bodies().ActivateBody(toJolt(id));
}

void PhysicsWorld::moveGrab(const glm::vec3& target) {
    if (m_impl->grab) m_impl->grab->target = m_impl->len(target);
}

void PhysicsWorld::setGrabOrientation(std::optional<glm::quat> rotation) {
    if (!m_impl->grab) return;
    m_impl->grab->orientation =
        rotation ? std::optional<JPH::Quat>(toJolt(glm::normalize(*rotation))) : std::nullopt;
}

void PhysicsWorld::endGrab() {
    if (!m_impl->grab) return;
    auto& bi = m_impl->bodies();
    const JPH::BodyID id = m_impl->grab->body;
    const float maxV = m_impl->tuning.maxThrowSpeed * m_impl->scale;
    const float maxW = m_impl->tuning.maxThrowAngularSpeed;
    const JPH::Vec3 v = bi.GetLinearVelocity(id);
    const JPH::Vec3 w = bi.GetAngularVelocity(id);
    if (v.Length() > maxV) bi.SetLinearVelocity(id, v.Normalized() * maxV);
    if (w.Length() > maxW) bi.SetAngularVelocity(id, w.Normalized() * maxW);
    m_impl->grab.reset();
}

std::vector<ImpactEvent> PhysicsWorld::takeImpacts() {
    std::vector<ImpactEvent> out;
    for (const auto& raw : m_impl->contacts.takeImpacts()) {
        out.push_back({fromJolt(raw.a), fromJolt(raw.b), m_impl->len(JPH::Vec3(raw.point)),
                       raw.speed * m_impl->invScale});
    }
    return out;
}

std::optional<BodyId> PhysicsWorld::grabbedBody() const {
    if (!m_impl->grab) return std::nullopt;
    return fromJolt(m_impl->grab->body);
}

std::optional<glm::vec3> PhysicsWorld::grabPoint() const {
    if (!m_impl->grab) return std::nullopt;
    JPH::RVec3 pos;
    JPH::Quat rot;
    m_impl->bodies().GetPositionAndRotation(m_impl->grab->body, pos, rot);
    return m_impl->len(pos + rot * m_impl->grab->localPoint);
}

std::uint32_t PhysicsWorld::bodyCount() const { return m_impl->system.GetNumBodies(); }

std::uint32_t PhysicsWorld::activeBodyCount() const {
    return m_impl->system.GetNumActiveBodies(JPH::EBodyType::RigidBody);
}

float PhysicsWorld::stepSeconds() const noexcept { return 1.0f / m_impl->tuning.stepHz; }

const PhysicsTuning& PhysicsWorld::tuning() const noexcept { return m_impl->tuning; }

} // namespace woodyn::physics
