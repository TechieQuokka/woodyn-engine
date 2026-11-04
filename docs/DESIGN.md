# WOODYN — Project Overview and Technical Design Draft

> **Tagline:** Real Wood. Real Physics.
> **Project:** `WOODYN`
> **Repository:** `woodyn-engine`
> **Namespace:** `woodyn`
> **Status:** Initial planning and architecture draft

---

## 1. Project Overview

WOODYN is a **real-time 3D rigid-body physics simulator** that recreates
toy wooden blocks made of real solid wood in a digital space.

Rather than simply placing blocks on screen, it aims to simulate each
block's mass, gravity, collisions, friction, rotation, and contact
stability, and to render the wood grain and surface characteristics
realistically.

The scope of the initial version is limited to **rigid-body physics**.
Bending, deformation, breakage, and fragmentation of wood are excluded
from the initial goals.

### Core Goals

- Realistic block falling, collision, sliding, and stacking
- Stable contact and friction simulation
- Hybrid material combining real wood photographs and procedural shaders
- Wood grain that continues across blocks as if cut from the same log
- Rendering and simulation performance that holds as block count grows
- Extensible architecture with physics and rendering engines decoupled

---

## 2. Technical Direction and Recommended Stack

| Area             | Recommended Technology                  | Role                                   |
| ---------------- | --------------------------------------- | -------------------------------------- |
| Main language    | C++20                                   | Engine core and performance-critical systems |
| Physics engine   | Jolt Physics                            | Rigid bodies, collision, contact, physics update |
| Graphics API     | Vulkan                                  | GPU rendering                          |
| Shaders          | GLSL or a Vulkan-compatible shader pipeline | Wood PBR and rendering             |
| Window & input   | SDL3                                    | Window, input, events                  |
| Math library     | GLM                                     | Vectors, matrices, quaternions         |
| Build            | CMake                                   | Build configuration and dependency management |
| Testing          | GoogleTest                              | Physics and system tests               |
| Texture format   | KTX2 (under review)                     | GPU-friendly texture asset management  |

### Principles for Technology Choices

- Jolt Physics is the primary prototyping target.
- NVIDIA PhysX 5 is kept as an alternative and a comparative benchmark target.
- The initial version is based on CPU multithreaded physics.
- GPU physics is considered only after real bottlenecks and requirements are confirmed.
- Vulkan is a rendering API, not a physics engine.
- Even when building our own engine, use a proven physics library rather
  than implementing the entire solver and collision handling from scratch.

---

## 3. Physics Simulation Scope

### 3.1 Initial Scope: Rigid Body

Physical phenomena covered in the initial version:

- Gravity
- Mass and density
- Linear and angular motion
- Inertia and torque
- Collision detection and collision response
- Static and kinetic friction
- Coefficient of restitution
- Contact constraints and stable stacking
- Sleeping and, if needed, Continuous Collision Detection (CCD)

Excluded from the initial version:

- Elastic deformation and bending of wood
- FEM-based continuum analysis
- Fracture and crack propagation
- Debris generation
- Fluids and aerodynamics
- Physical analysis of internal wood fiber structure

### 3.2 Unit System

Physics simulation uses SI units.

| Item                  | Standard                    |
| --------------------- | --------------------------- |
| Length                | meter (m)                   |
| Mass                  | kilogram (kg)               |
| Time                  | second (s)                  |
| Gravity acceleration  | approx. 9.80665 m/s²        |
| Physics update        | experiment at 120 Hz first  |

120 Hz is an initial experimental value and will be tuned while
validating accuracy and performance. A fixed timestep is used and is
decoupled from the rendering cycle.

### 3.3 Wood Material Properties

Physical properties are managed separately per wood species.

Example properties:

- Density
- Static friction coefficient
- Kinetic friction coefficient
- Coefficient of restitution

These values are not fixed as arbitrary constants. They are measured and
calibrated against real wood samples and contact conditions. Friction can
vary with surface finish, moisture, contact direction, load, and so on.

---

## 4. Rendering and Wood Material Goals

The rendering target is children's wooden blocks made of natural solid wood.

Features to express:

- Natural wood grain and fiber direction
- Difference between side faces and cut faces
- Growth rings and end grain on cross-sections
- Surface roughness and micro-relief
- Subtle color variation
- Slight rounding of machined edges
- Realistic lighting, shadows, and reflections

### 4.1 Hybrid Material

Blend photographed wood textures with procedural shaders.

| Data              | Role                                   | Base Approach                              |
| ----------------- | -------------------------------------- | ------------------------------------------ |
| Albedo/Base Color | Wood color and large-scale pattern     | Real photo texture                         |
| Normal            | Surface micro-relief                   | Photo-based with procedural correction     |
| Roughness         | Surface roughness                      | Photo-based with procedural modulation     |
| Height            | Fine surface height                    | Procedural or photo-based                  |
| End Grain         | Growth rings on cross-sections         | Real cross-section photo + procedural supplement |
| Fiber Mask        | Fiber direction and region separation  | Procedural data                            |

Do not randomly transform real textures in a way that breaks the
structural continuity of the log. Repetition-reduction processing is
applied only in a limited way, based on the shared log coordinates.

### 4.2 PBR Principles

- Wood is treated as a non-metal material by default.
- Metallic defaults to 0.
- Roughness may vary by surface location and wood finish.
- Normal and Height are not exaggerated and match real surface scale.
- Use HDR environment lighting and a physically based lighting model.
- Manage tone mapping and color space handling consistently.

---

## 5. Grain Continuity: Core Design

A key visual feature of WOODYN is that **the grain continues across
multiple blocks as if they were cut from a single log**.

To achieve this, avoid relying solely on independent per-block UV
texture coordinates.

### 5.1 Wood Source Concept

`WoodSource` represents a single log or raw wood material.

Information to manage:

- Unique ID
- Log dimensions
- Log reference coordinate system
- Fiber direction
- Wood species and visual material
- Texture scale and mapping parameters

Each `WoodBlock` references which region of which `WoodSource` it was cut from.

### 5.2 Coordinate Handling

- Transform positions on the block surface from block-local coordinates
  into shared log coordinates.
- Perform texture sampling based on the shared log coordinates.
- Even when a block moves or rotates, its original cut location and
  grain within the log are preserved.
- Distinguish cut faces from side faces by considering both the surface
  orientation and the fiber direction.

Important: using current world coordinates directly as texture
coordinates makes the grain appear to slide when a block moves. The log
reference coordinates must be preserved.

### 5.3 Cut Faces and Side Faces

Classify end grain and side faces using the relationship between the
fiber direction and the surface normal.

- Cut face perpendicular to the fiber direction: End Grain
- Face roughly parallel to the fiber direction: Longitudinal Grain

Rather than a simple binary classification, use a blending region so
boundaries do not look unnatural.

---

## 6. Rendering Architecture

Recommended modules:

1. **Wood Source Model**
   - Manages log information, fiber direction, material, and cut regions
2. **Wood Coordinate System**
   - Manages log reference coordinates and block transforms
3. **Texture Pipeline**
   - Photo texture loading, mipmaps, compression, and asset management
4. **Procedural Pipeline**
   - Fiber patterns, noise, growth rings, and micro-surface modulation
5. **Wood Material Shader**
   - Texture blending, cut/side face classification, PBR input generation
6. **Vulkan Renderer**
   - Mesh submission, instancing, lighting, shadows, and post-processing

### Shader Processing Stages

1. Grain Mapping: compute log coordinates and fiber direction
2. Texture Blending: combine photo-based textures with procedural patterns
3. Surface Reconstruction: build Normal, Roughness, Height
4. PBR Lighting: compute lighting and environment reflections

### Geometric Detail

Edge rounding is not handled by shader effects alone; real bevel meshes
are used. Start by comparing performance and appearance with 2–3
segments. Normal processing such as Weighted Normals will also be considered.

---

## 7. Decoupling Physics and Rendering

The physics engine and rendering engine are kept independent of each other.

- Jolt Physics computes the physical state of objects.
- The Simulation Layer manages time and state updates.
- The Renderer reads the physics state and displays it.
- Rendering may use interpolation between physics states.
- Visual material properties and physical material properties are stored separately.

For example, wood color and grain are rendering data, while density,
friction, and restitution are physics data. The two may share the same
wood species ID but must be tunable independently.

---

## 8. Performance Design

Initial optimization candidates:

| Area                     | Technique                          |
| ------------------------ | ---------------------------------- |
| Block rendering          | GPU Instancing                     |
| Materials                | Material Batching                  |
| Textures                 | Consider Texture Array or Atlas    |
| Culling                  | Frustum Culling                    |
| Resting objects          | Physics Sleeping                   |
| Collision candidate search | Broadphase                       |
| Physics update           | CPU multithreading                 |
| Memory                   | GPU Resource Pool and reuse        |
| Shaders                  | Compile only required variants     |

Optimization is driven by profiling results. Measure block count, physics
step time, rendering time, draw calls, and memory usage.

Even with GPU instancing, a data layout must be designed to efficiently
pass per-log coordinate transforms and material references. The actual
GPU buffer layout is validated against Vulkan alignment and descriptor rules.

---

## 9. Recommended Project Directory

```text
Woodyn/
├── Engine/
│   ├── Core/
│   │   ├── Application.cpp
│   │   ├── TimeManager.cpp
│   │   └── ThreadPool.cpp
│   │
│   ├── Physics/
│   │   ├── PhysicsWorld.cpp
│   │   ├── PhysicsBody.cpp
│   │   └── CollisionSystem.cpp
│   │
│   ├── Rendering/
│   │   ├── VulkanRenderer.cpp
│   │   ├── RenderPipeline.cpp
│   │   ├── MeshManager.cpp
│   │   └── InstanceBuffer.cpp
│   │
│   ├── Materials/
│   │   ├── WoodMaterial.cpp
│   │   ├── WoodSource.cpp
│   │   ├── GrainMapping.cpp
│   │   └── TextureManager.cpp
│   │
│   └── Geometry/
│       ├── BoxMesh.cpp
│       └── BevelMesh.cpp
│
├── Shaders/
│   ├── wood.vert
│   ├── wood.frag
│   ├── grain.glsl
│   ├── triplanar.glsl
│   └── pbr.glsl
│
├── Assets/
│   ├── Wood/
│   │   ├── Oak/
│   │   ├── Beech/
│   │   └── Pine/
│   └── Environment/
│
├── Tests/
│   ├── PhysicsTests/
│   ├── GrainContinuityTests/
│   └── RenderingTests/
│
└── CMakeLists.txt
```

---

## 10. Development Roadmap

### Phase 1 — Project Foundation

- Set up C++20 and CMake project
- SDL3 window and input handling
- Integrate Jolt Physics
- Basic logging and test environment

### Phase 2 — Physics Prototype

- Create rigid-body blocks
- Gravity and collision
- Friction and restitution settings
- Verify block stacking stability
- Fixed-timestep simulation

### Phase 3 — Log Coordinates and Material Data

- Design and implement `WoodSource`
- Link `WoodBlock` to log cut regions
- Shared log coordinate transform
- Fiber direction and cut-face classification
- Grain continuity tests

### Phase 4 — Wood Rendering

- Basic Vulkan renderer
- Apply photo-based albedo
- End grain textures
- Normal and Roughness
- Procedural grain correction
- PBR lighting and bevel meshes

### Phase 5 — Performance and Quality

- GPU Instancing
- Measure multithreaded physics performance
- Large-scale block benchmarks
- Contact stability validation
- Texture and shader optimization

---

## 11. Validation Plan

Simulation realism is not judged solely by whether it looks good.
Reproducible tests are created.

### Physics Tests

- Compare free-fall time under known conditions
- Measure the angle at which a block starts sliding on an incline
- Check velocity and energy change before and after collision
- Check jitter and penetration after stacking blocks
- Verify result stability across repeated runs

### Rendering Tests

- Grain continuity across blocks from the same log
- Texture stays fixed when blocks rotate and move
- Natural distinction between side and cut faces
- Wood material consistency under various lighting
- Check texture repetition and tiling artifacts

### Performance Tests

- Physics step time by block count
- Rendering frame time by block count
- CPU thread utilization
- GPU memory and texture memory
- Performance under concentrated collisions

Test results are recorded together with reference hardware, build
configuration, and simulation settings.

---

## 12. Completion Criteria for the Initial Version

The initial MVP is considered complete when:

- Blocks can be created and moved.
- Blocks move naturally under gravity and collision.
- Sliding and stopping are expressed according to friction.
- Multiple stacked blocks remain stable.
- A material based on real wood photographs is applied.
- Grain continuity is maintained between blocks from the same log.
- Grain does not slide on the surface when blocks move.
- Physics and rendering operate independently.

---

## 13. Open Decisions

The following items require experimentation or further discussion before implementation.

1. ~~Which wood species to support first: Beech, Oak, or Pine~~ — resolved, see §15 (D-07)
2. ~~How to photograph textures or secure licenses~~ — resolved, see §15 (D-08)
3. ~~Reference for the log coordinate system and representation of cut data~~ — resolved, see §15 (D-32..D-34)
4. Blending method between cut-face and side-face textures
5. ~~Target block count and reference hardware~~ — resolved, see §15 (D-10)
6. Benchmark of Jolt Physics vs. PhysX under identical conditions
7. ~~Initial feature scope of the Vulkan renderer~~ — resolved, see §15 (D-37, D-38)
8. Real wood samples and measurement methods for validating physics accuracy

---

## 14. Project Identity

**WOODYN**
**Real Wood. Real Physics.**

WOODYN is a standalone 3D simulation engine that combines realistic
rigid-body physics with wood material rendering.

It initially focuses on wooden block simulation, but the log coordinate
system, physical materials, and rendering system are designed as
independent modules so it can later expand to a variety of wood-based simulations.

---

## 15. Decisions Log

Decisions made after the initial draft. Where a decision conflicts with an
earlier section, this log takes precedence.

**Product framing:** WOODYN is a virtual toy. The block set must faithfully
serve the role of a children's wooden block toy and allow building
structures such as a simple house.

| ID   | Topic               | Decision | Date |
| ---- | ------------------- | -------- | ---- |
| D-01 | Target platform     | Linux only. | 2026-11-04 |
| D-02 | Dependency management | CMake `FetchContent` (pinned versions, latest stable releases). The Vulkan SDK is installed on the system separately. | 2026-11-04 |
| D-03 | Directory layout    | Public headers in `engine/include/woodyn/...`, implementation in `engine/src/...`. Lowercase directory names, `.hpp` header extension. Supersedes the layout in §9. | 2026-11-04 |
| D-04 | Compiler            | Latest upstream LLVM Clang (apt.llvm.org) as the primary compiler; GCC 13 kept as a secondary build for compatibility. | 2026-11-04 |
| D-05 | Block shapes        | Initial set: cube/cuboid, triangular prism, cylinder, half-cylinder, arch, cone/pyramid. The arch is concave and is implemented as a Jolt compound shape of convex parts. | 2026-11-04 |
| D-06 | Base module unit    | 3 cm. All block dimensions are multiples of the 3 cm module. Jolt physics settings (convex radius, penetration slop, speculative contact distance) are tuned to this scale, based on the smallest block in the set. | 2026-11-04 |
| D-07 | First wood species  | Beech first. Oak added second, early, as a grain-continuity validation species. | 2026-11-04 |
| D-08 | Texture sourcing    | CC0 PBR texture libraries (e.g. ambientCG, Poly Haven). Beech end grain supplemented procedurally. | 2026-11-04 |
| D-09 | Interaction model   | Physics-based mouse grab-and-drag (constraint/spring) as the default, with optional assists such as 90° rotation snapping and a placement preview. | 2026-11-04 |
| D-10 | Performance target  | ~1,000 blocks at 60 fps on the reference hardware (NVIDIA GeForce RTX 3060). Requires instancing and sleeping. | 2026-11-04 |

### Implementation Decisions (Phase 1–2)

Made autonomously during the non-stop Phase 1–2 run, backed by measurements.

| ID   | Topic               | Decision | Date |
| ---- | ------------------- | -------- | ---- |
| D-11 | Physics rate        | **480 Hz** fixed step (supersedes the 120 Hz starting value in §3.2). 3 cm blocks have a natural time scale √(L/g) ≈ 55 ms; at 120 Hz towers of 7+ cubes collapse from solver error. Measured (zero spawn gap, Release): 120 Hz → 6-cube max; 240 Hz → 10; 480 Hz → 15; 600 Hz → 20. Cost is ~1.2 ms per step for 1,000 blocks, ≈ 9.7 ms per 60 fps frame. Raising solver iterations or rescaling units did **not** help; only time resolution does. | 2026-11-04 |
| D-12 | Logging library     | spdlog 1.17.0, hidden behind `woodyn::log` (std::format front end). Resolves the deferred logging decision. | 2026-11-04 |
| D-13 | Physics API boundary | Jolt is a private dependency behind `physics::PhysicsWorld` (PIMPL); public API uses GLM types, SI units, +Y up. `PhysicsTuning::lengthScale` can rescale Jolt's internal units (measured: no effect on stacking, kept at 1). | 2026-11-04 |
| D-14 | Jolt tuning for 3 cm blocks | Convex radius 1 mm, speculative distance 2 mm, penetration slop 0.5 mm, max penetration correction 1 cm, sleep threshold 3 mm/s, restitution threshold 0.2 m/s, 12 velocity / 4 position iterations, linear damping 0, angular damping 0.05, LinearCast CCD for dynamic blocks. See `PhysicsTuning.hpp`. | 2026-11-04 |
| D-15 | Friction model      | Jolt has a single friction coefficient per body (no static/kinetic split; §3.3 lists both) combined by geometric mean; restitution combined by max. Per-species values are PROVISIONAL literature values (Beech: 720 kg/m³, μ 0.45, e 0.50; Oak: 710 kg/m³, μ 0.50, e 0.45) until calibrated per §11. | 2026-11-04 |
| D-16 | Block catalog       | 9 types: Cube 1×1×1, Brick 2×1×1, Beam 4×1×1, RoofPrism 2×1×1, Cylinder ⌀1×2, HalfCylinder 2×1×1, Arch 4×2×1 (opening radius 1, 2 legs + 12 convex slices), Cone ⌀1×1, Pyramid 1×1×1 (modules). Curves use 24 segments. One convex decomposition feeds both collision and render mesh. | 2026-11-04 |
| D-17 | Dependency versions | Jolt 5.6.0, SDL 3.4.20, GLM 1.0.3, spdlog 1.17.0, GoogleTest 1.18.0 (all SHA-256 pinned); toolchain Clang 23.1.6, CMake 4.4.5, Ninja 1.13.2, Vulkan SDK 1.4.363.1. Jolt GPU compute back-ends off; RTTI/exceptions on. | 2026-11-04 |
| D-18 | Determinism         | `CROSS_PLATFORM_DETERMINISTIC` stays OFF. Same-build determinism is verified by tests, including independence from worker thread count. | 2026-11-04 |
| D-19 | Prototype tooling   | `woodyn_viewer`: SDL3 debug viewer drawing flat-shaded meshes with SDL_Renderer (temporary until the Phase 4 Vulkan renderer). `woodyn_bench`: headless scenario runner with tuning overrides. | 2026-11-04 |

### Toy Interaction & Realism Decisions (step A)

| ID   | Topic               | Decision | Date |
| ---- | ------------------- | -------- | ---- |
| D-20 | Grab-and-drag (implements D-09) | Spring-damper "hand" applied as an impulse at the grabbed point every physics step: 8 Hz, critically damped, gravity compensated, pull capped at 40 m/s² (blocks cannot be shoved through others). Orientation assist snaps to the nearest of the 24 axis-aligned orientations; Q/E turn ±90° about Y, R tips 90°. Free mode damps swinging. Drop preview = downward shape cast of the held block. Release keeps velocity (throwing works). | 2026-11-04 |
| D-21 | Rolling resistance  | WOODYN-side torque `c_rr · m · g · r_inner` opposing the spin of every dynamic body in contact (contact listener + post-step impulse, never reversing spin). PROVISIONAL c_rr: Beech 0.006, Oak 0.007. Validated against a = ⅔·c_rr·g for a rolling cylinder. | 2026-11-04 |
| D-22 | Edge rounding       | 1.5 mm edge radius (`blocks::kEdgeRadius`) for both the render mesh and the Jolt convex radius, so visuals and collision match. Render meshes: filleted-profile extrusion (cuboids, prism, half cylinder, arch as one seamless concave piece), revolution (cylinder, cone with spherical apex), Minkowski rounding (pyramid). Sharp corners (< 90°) are pulled inside the bounding box by r·(1/sin(½θ) − 1), as on real blocks. Supersedes the Phase 4 bevel-mesh plan in §6 for geometry. | 2026-11-04 |
| D-23 | Contact cache       | Jolt body-pair cache rotation threshold 0.25° (default 2°). With 2°, a rolling 15 mm cylinder reused stale contacts that pushed it forward and cancelled rolling resistance. No measurable cost (1,000 blocks: 1.20 ms/step). | 2026-11-04 |
| D-24 | Arch collision decomposition | One full-width top slab + 2 legs + slices under the opening; compound parts use a 0.3 mm convex radius so part seams never form grooves on resting surfaces. | 2026-11-04 |
| D-25 | Viewer realism      | Debug viewer: per-vertex lighting in linear space (warm key light, sky/ground ambient, satin specular), planar floor shadows via a mask texture, 2× supersampling, long triangles tessellated (≤ 8 mm) for painter's-algorithm accuracy. Still temporary until Phase 4. | 2026-11-04 |

### Playroom Decisions (step B)

| ID   | Topic               | Decision | Date |
| ---- | ------------------- | -------- | ---- |
| D-26 | Grip & look (user choices) | **Natural grip**: a held block keeps the pose it was picked up in (pull through the center of mass, compliant orientation spring); alignment only while **Space** is held, and gradual. **Color**: blocks are **stained wood** (option C): dye color × wood light/dark variation, so grain will show through in Phase 3–4. Palette: natural, red, yellow, blue, green, orange, purple. Stain is visual only. Supersedes the auto-snap of D-20. | 2026-11-04 |
| D-27 | Impact sound (user choice B) | Physics-based **modal synthesis**: Jolt contact events (approach speed along the normal) → each block rings with its own modes (size-dependent pitch, bar-like vs. compact mode ratios, wood damping), static surfaces add theirs (foam mat thud, fence knock), plus a filtered-noise contact click. Loudness ∝ speed^1.1 · mass^0.25; stereo pan and distance from the camera; per-pair 30 ms rate limit. Mode tables are PROVISIONAL (calibrate with recordings, §11). `woodyn_bench --wav` renders audible test files. | 2026-11-04 |
| D-28 | Play area           | 1.2 m × 1.2 m foam mat, 4.5 cm wooden fence, invisible 1.2 m walls + ceiling, 30 cm thick. Throws capped at 4 m/s (40 rad/s). Out-of-bounds safety net returns any escaped block (logged; tests require it to never trigger). Verified: all 9 block types thrown at 4/12/30 m/s in 6 directions never leave. Camera target and hand are clamped to the area. | 2026-11-04 |
| D-29 | Restitution combine | `min` of both bodies (softer surface wins): a wooden block barely bounces on the foam mat; wood on wood keeps 0.5. Supersedes the `max` rule noted in D-15. | 2026-11-04 |
| D-30 | Playroom skin       | Cream-to-peach walls with pastel polka dots, light oak floorboards, EVA mat tiles in pink/mint/sky blue/lemon, rounded beech fence, warm lighting. Skin colors are data (`viewer::Skin`) so more skins can be added. | 2026-11-04 |
| D-31 | Controls (user approved) | Camera: WASD, right-drag orbit, middle/Shift+right-drag pan, wheel zoom. Holding: Shift+mouse or wheel lift, Ctrl+mouse turn in hand, Space align, Q/E/R 90° steps. 1–9 spawn at the cursor (cycling colors), C recolor, O species, F2–F5 scenes, Delete clear, **Ctrl+Z / Ctrl+Y undo/redo** (64 snapshots), P pause, M mute. Alt is avoided (GNOME uses Alt+drag). | 2026-11-04 |

### Phase 3 Decisions — Log Coordinates & Grain

| ID   | Topic               | Decision | Date |
| ---- | ------------------- | -------- | ---- |
| D-32 | Log model (`wood::WoodSource`) | A log is a deterministic 3D grain field in log coordinates: +Z along the fibers from one end, X/Y across the section, slightly off-center pith. Default log: Ø 36 cm × 1.2 m. Field: growth rings (distance from pith with slow 3D wobble, per-year width variation, abrupt latewood), medullary ray flecks (beech: many small; oak: fewer), ring-porous pore band (oak), two-scale fiber streaks, large-scale tone variation. Hash-based gradient noise, no tables, reproducible per seed. Species values PROVISIONAL until matched to photos (D-08). | 2026-11-04 |
| D-33 | Cutting (`wood::WoodLibrary`) | The library ("sawmill") splits each log's section into 6.5 cm plank cells and cuts blocks one after another along a cell with **zero kerf**, so consecutive blocks share their contact faces exactly. Fibers follow the block's longest axis (ties → X) unless requested; the block's up axis points away from the pith, **snapped to the plank grid** (a free radial direction rotated wide blocks out of their cell — caught by tests). New log when full. Every block gets its own cut automatically; towers cut cubes with vertical fibers in stacking order so a tower reads as one beam. | 2026-11-04 |
| D-34 | Block → log link (`wood::WoodCut`) | Fixed block-local → log transform stored per block (and in undo snapshots). Grain is sampled in block-local coordinates only, so it can never slide when blocks move (§5.2). | 2026-11-04 |
| D-35 | End grain & stain on grain | `endGrainFactor` = smoothstep of \|normal · fiber\| between 0.55 and 0.85 (smooth over rounded edges). End grain is 16 % darker and soaks up more dye. `wood::surfaceAlbedo` (grain → end grain → stain) is the single source of truth for every renderer; the Vulkan shader (Phase 4) must port it. | 2026-11-04 |
| D-36 | Grain in the debug viewer | Per-block grain textures baked once into a 4096² atlas: each block type's mesh is unwrapped into charts by dominant face direction (0.5 mm texels, 2-texel dilation); texels store block-local surface points and normals, so baking samples the exact surface. Parallel baking; atlas re-packs when full. F6 = grain demo scene. | 2026-11-04 |

### Phase 4 Decisions — GPU Renderer

| ID   | Topic               | Decision | Date |
| ---- | ------------------- | -------- | ---- |
| D-37 | Graphics API (user choice) | **SDL3 GPU API** (Vulkan backend on Linux) instead of raw Vulkan: far less code, same SPIR-V shaders, enough features (instancing, MSAA, shadow maps, compute). An SDL GPU renderer on the same device composites the 3D image with the HUD and handles presentation and headless screenshots. SDL GPU normalizes NDC to +Y up and texture UV (0,0) to top-left on every backend. Resolves §13 item 7. | 2026-11-04 |
| D-38 | Feature scope (user choice C) | Sun shadow map 4096² (0.44 mm texels) with 16-tap Poisson PCF; depth/normal prepass; SSAO (16 samples, 2.2 cm radius) + screen-space contact shadows (1.2 cm march) with depth-aware blur; MSAA 4× HDR (RGBA16F) forward pass; GGX/Smith PBR with clear-coat lacquer; IBL; thin-lens depth of field ("tilt-shift" miniature look); 5-level bloom; Khronos PBR Neutral tone mapping (keeps toy colors true) + vignette. F7/F8/F9 toggle DOF / AO+contact / bloom. | 2026-11-04 |
| D-39 | Grain on the GPU (user choice: hybrid, staged) | Stage 1 (done): `wood::surfaceAlbedo` ported to GLSL and evaluated per pixel (no resolution or block-count limit), plus micro-relief normals from latewood/fiber height (finite differences in log space) and grain-driven roughness (latewood/rays smoother, end grain rougher). Bit-compatible integer hashing; a compute-shader probe test compares 600 GPU samples with the CPU reference (≥ 97 % within 2/255). Stage 2 (next): CC0 photo micro-detail layer (D-08). | 2026-11-04 |
| D-40 | Lighting setup | Warm sun through a window (direction (−0.45, 1, 0.35), radiance ≈ 3); IBL from an analytic playroom radiance model (cream walls, oak floor, white ceiling, bright window) prefiltered on the CPU into a 64² GGX cubemap mip chain + SH9 irradiance; Karis analytic environment BRDF. Toy blocks: satin base (roughness ≈ 0.46) + 0.55 clear coat (0.22); floorboards: varnish (coat 0.9, 0.12). | 2026-11-04 |
| D-41 | Room in PBR | Oak floorboards (staggered 36 × 12 cm planks) and the beech fence are wood instances using the grain shader; walls are painted (roughness 0.85) with polka dots; the EVA mat is 1 cm thick with closed-cell speckle, soft relief and 1 mm seams between 30 cm tiles. | 2026-11-04 |
| D-42 | Shaders & renderer selection | GLSL 4.60 → SPIR-V via glslc at build time (`build/shaders`). `woodyn_viewer` uses the GPU renderer by default; `--renderer soft` keeps the software debug renderer (also the automatic fallback without Vulkan). GPU tests (`woodyn_gpu_tests`, label `gpu`) skip when no device exists. | 2026-11-04 |

### Interaction Refinements (after Phase 4, stage 1)

| ID   | Topic               | Decision | Date |
| ---- | ------------------- | -------- | ---- |
| D-43 | Toy shelf (user choice) | A shelf at the bottom of the screen shows the 9 block shapes as shaded icons in the color the next block will get, plus the 7 color swatches. Pressing a shape spawns the block on the mat under the pointer and puts it straight into the hand (release to drop); swatches pick the next color. Tab hides the shelf; 1–9 keep working. | 2026-11-04 |
| D-44 | Fine rotation (user request) | While holding: Q/E turn about the vertical, R/F tip about the horizontal axis closest to the camera's right. A tap turns **1°**; holding keeps turning after 0.3 s at 20°/s ramping to 90°/s; Shift = 90° steps. Ctrl+wheel = 1° per notch; Ctrl+mouse turns freely (0.004 rad/px), Ctrl+Shift+mouse is 4× finer. The HUD shows heading and tilt in degrees. | 2026-11-04 |
| D-45 | Fullscreen | F11 toggles fullscreen; the GPU renderer and the composited texture follow the new output size. | 2026-11-04 |

### Landing Feel (user report: tilted blocks did not settle naturally)

Measured: a cube dropped at 25° onto the mat hit with a corner, flipped onto a face, then **rocked back up 19° over the leading edge** before settling (0.17 s of wobble). Final poses were always correct; the motion was what looked wrong. Rigid-floor physics is right for wood on wood, but an EVA foam mat compresses and swallows the rotational kick.

| ID   | Topic               | Decision | Date |
| ---- | ------------------- | -------- | ---- |
| D-46 | Surface rolling resistance | Rolling resistance uses max(c_rr of the block, c_rr of the touched surface): foam mat 0.08, wood 0.006 (PROVISIONAL). Measured alone it barely reduced the rocking (19° → 13–15°) while strongly limiting rolling at higher values, so it stays moderate. | 2026-11-04 |
| D-47 | Soft-impact absorption | Contacts with a **soft** surface (the foam mat) approaching faster than 0.15 m/s mark the block; after that step it keeps 50 % of its spin. Slow contacts (resting, gravity tipping, rolling) are untouched. Result: worst rebound 14.9° → 2.2°, at rest 35 ms after landing flat, tip-over onto the face within ~50 ms, rolling distance unchanged. Tried and rejected: restitution 0 (no effect: rebound 16.4°) and reduced inverse inertia on soft contacts (rebound 44.9°: the floor then cannot stop the spin). Regression tests: `SoftLanding.*`. | 2026-11-04 |

### Phase 4 Stage 2 — Photo Micro Detail

| ID   | Topic               | Decision | Date |
| ---- | ------------------- | -------- | ---- |
| D-48 | Photo source | ambientCG **Wood058** (CC0, 40 × 40 cm sample): uniform fine fibers and pores without macro figure. Rejected: Wood048 (strong flat-sawn figure that would fight the procedural rings), Wood054 (scratches), Wood056 (too smooth). Provenance in `assets/wood/SOURCES.md`; reproducible with `tools/make_fiber_detail.py`. | 2026-11-04 |
| D-49 | Packed detail texture | 1024² RGBA PNG: R = high-passed luminance ratio (features > ~5 mm removed, so the photo adds no tone or rings), G/B = normal x/y, A = roughness detail. Loaded with stb_image (pinned commit, SHA-256), mipmapped, 8× anisotropic, repeat. | 2026-11-04 |
| D-50 | Applying the detail | GPU only (not part of the CPU `surfaceAlbedo` reference): projected in **log space** onto two planes containing the fiber axis, blended by the normal, so fibers always run along the real grain and stay continuous across blocks cut from the same log; per-log offset hides repetition; one tile = 20 cm of wood; strength 0.7 on side grain, none on end grain. Modulates albedo, normal and roughness. F10 toggles it. Completes D-39. | 2026-11-04 |

### Creating & Removing Blocks (user design)

| ID   | Topic               | Decision | Date |
| ---- | ------------------- | -------- | ---- |
| D-51 | Placement mode (user choice A) | A shelf shape or 1–9 enters placement mode: a translucent ghost follows the pointer, lowered onto whatever is under it (mat or other blocks) by a shape cast (`PhysicsWorld::placementPreview`); red when the spot is occupied or the block would stick out of the play area. Q/E/R/F (1°, hold = continuous, Shift = 90°), Ctrl+wheel/mouse and Space rotate the ghost before placing. Left click places the block gently (at rest, no drop) and leaves the mode; Shift+click keeps placing; right click (not a drag) or Esc cancels. The ghost starts square to the camera. Replaces "spawn straight into the hand" (D-43). | 2026-11-04 |
| D-52 | Removing (user choice A + B) | Drop a held block onto the **toy box** at the right end of the shelf to put it away (soft knock sound), or press **Delete** on the held/hovered block. **Shift+Delete** clears everything (moved from Delete). Every removal is undoable with Ctrl+Z. `Simulation::removeBlock` keeps block indices consistent. | 2026-11-04 |
| D-53 | Picking through invisible bounds (bug fix) | User report: with the camera zoomed out or orbited outside the 0.6 m walls, clicking placed nothing and a huge red ghost appeared. Cause: the pointer ray hit the **invisible walls/ceiling** first, so the ghost sat inside a wall right in front of the camera (perspective made it look huge and "resize" as the pointer moved). Fix: invisible bounds live in their own object layer (`kBounds`) that still collides with blocks but is ignored by all picking ray casts (`SurfaceProperties::pickable = false`). Verified at camera distances 0.5 / 0.75 / 1.2 / 1.6 m; regression tests `Placement.PickingIgnoresInvisibleWallsAndCeiling`, `Placement.InvisibleWallsStillStopBlocks`. Lesson: interaction must be verified from several camera positions, not one. | 2026-11-04 |

### Known Issues / Open Items (after Phase 4)

- **End grain has no photo detail** (no suitable CC0 end-grain photo; procedural only).
- **Oak uses the same fiber photo as beech** (fine for micro detail; a ring-porous photo would be better).
- **No GPU frustum culling / LOD yet:** fine for the 1,000-block target on the RTX 3060, revisit in Phase 5.
- **Sound calibration:** mode tables are perceptual estimates; no sliding/rolling sounds yet.
- **Very tall towers:** 20-cube towers need ≥ 600 Hz (D-11).
- **Static vs kinetic friction:** single coefficient (D-15).
- **Material values:** all physical properties are provisional (§3.3, §11).

### Deferred Decisions

To be decided when the relevant phase begins:

- Project license
