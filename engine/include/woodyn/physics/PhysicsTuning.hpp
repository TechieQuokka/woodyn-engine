#pragma once

#include "woodyn/core/Units.hpp"

#include <cstdint>

namespace woodyn::physics {

/// Jolt parameters re-tuned for 3 cm toy blocks (D-06).
///
/// Jolt's defaults target meter-scale game objects: a 5 cm convex radius,
/// 2 cm penetration slop and 2 cm speculative distance are as large as an
/// entire WOODYN cube. Every length below is scaled to the 3 cm module.
struct PhysicsTuning {
    // --- Internal unit scale -----------------------------------------------------
    /// Jolt internal length units per meter. The public API is always SI; all
    /// lengths (and length-derived values) below are given in meters and are
    /// multiplied by this factor before reaching Jolt. Dynamics are scale
    /// invariant; only Jolt's absolute internal tolerances are affected.
    float lengthScale = 1.0f;

    // --- Simulation rate -------------------------------------------------------
    float stepHz = units::kDefaultPhysicsRateHz;
    int collisionStepsPerUpdate = 1;

    // --- Shapes ----------------------------------------------------------------
    /// Rounding radius used for collision of convex shapes [m]. Equals the
    /// visual edge radius of the blocks (blocks::kEdgeRadius, 1.5 mm), so what
    /// you see is what collides.
    float convexRadius = 0.0015f;
    /// Convex radius of the parts of compound blocks (arch). Kept small so the
    /// seams between parts do not form grooves.
    float compoundPartConvexRadius = 0.0003f;
    /// Max deviation allowed when Jolt shrinks a hull for its convex radius [m].
    float hullMaxErrorConvexRadius = 0.0005f;
    /// Tolerance used when building convex hulls [m].
    float hullTolerance = 1.0e-5f;

    // --- Contacts (Jolt PhysicsSettings) ----------------------------------------
    float speculativeContactDistance = 0.002f; // default 0.02 m
    float penetrationSlop = 0.0005f;           // default 0.02 m
    float maxPenetrationDistance = 0.01f;      // default 0.2 m
    float manifoldTolerance = 1.0e-4f;         // default 1e-3 m
    float bodyPairCacheMaxDeltaPosition = 0.0002f;  // default 1 mm
    /// Max relative rotation for reusing last step's contacts [deg] (default 2).
    /// A 15 mm cylinder rolling 2 deg moves its contact point 0.5 mm: stale
    /// cached contacts then push it along and cancel rolling resistance.
    float bodyPairCacheMaxDeltaRotationDegrees = 0.25f;
    float contactPointPreserveLambdaMaxDist = 0.001f; // default 1 cm
    float minVelocityForRestitution = 0.2f;    // default 1 m/s (too high for small drops)
    float baumgarte = 0.2f;

    // --- Solver ------------------------------------------------------------------
    std::uint32_t velocitySteps = 12; // default 10
    std::uint32_t positionSteps = 4;  // default 2 (better stacking)
    bool constraintWarmStart = true;
    bool useBodyPairContactCache = true;
    bool useManifoldReduction = true;
    bool useLargeIslandSplitter = true;

    // --- Sleeping -----------------------------------------------------------------
    float timeBeforeSleep = 0.5f;
    /// A 3 cm/s threshold (default) is one block length per second: far too
    /// high for 3 cm blocks, which could fall asleep while still wobbling.
    float pointVelocitySleepThreshold = 0.003f;

    // --- Bodies --------------------------------------------------------------------
    float linearDamping = 0.0f;   // Jolt default 0.05 would distort free fall
    float angularDamping = 0.05f; // small, helps settle (PROVISIONAL)
    /// Dynamic blocks use continuous collision (linear cast). A cube falling
    /// from 1 m moves ~3.7 cm per 120 Hz step, more than its own size.
    bool continuousCollision = true;

    // --- Rolling resistance (D-21) -------------------------------------------------
    bool rollingResistance = true;

    // --- Grab-and-drag (D-09, D-20) ----------------------------------------------------
    float grabFrequencyHz = 8.0f;        // spring stiffness of the "hand"
    float grabDampingRatio = 1.0f;       // 1 = critically damped
    float grabMaxAcceleration = 40.0f;   // [m/s^2] caps the pull so blocks are not shoved through others
    bool grabCompensateGravity = true;   // a hand holds the block's weight
    float grabRotationFrequencyHz = 6.0f; // orientation assist stiffness
    float grabHeldAngularDamping = 6.0f;  // [1/s] extra damping of swinging while held
    /// How fast the grip's angular velocity follows the orientation target [1/s].
    /// Finite, so knocks against other blocks can still turn the block a little.
    float grabRotationResponse = 40.0f;
    float maxThrowSpeed = 4.0f;           // [m/s] release speed cap (a child's throw)
    float maxThrowAngularSpeed = 40.0f;   // [rad/s]

    // --- Impact events (sound, D-27) --------------------------------------------------
    float impactMinSpeed = 0.03f;          // [m/s] new contacts slower than this are silent
    float impactPersistedMinSpeed = 0.30f; // [m/s] re-impacts on an existing contact (rocking, bouncing)

    // --- Capacity (D-10 target: ~1,000 blocks) --------------------------------------
    std::uint32_t maxBodies = 8192;
    std::uint32_t maxBodyPairs = 65536;
    std::uint32_t maxContactConstraints = 32768;

    /// Worker threads for Jolt's job system. <= 0 means hardware threads - 1.
    int workerThreads = -1;
};

} // namespace woodyn::physics
