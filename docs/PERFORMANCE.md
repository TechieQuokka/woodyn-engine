# WOODYN Performance (Phase 5)

Target (D-10, judged as D-55): **~1,000 blocks at 60 fps** (16.7 ms per
frame) on the reference hardware during toy play. That means 1,000 blocks that
are mostly asleep, with collapses that set a few hundred in motion. The extreme
case (all 1,000 moving at once) may slow down briefly but must never stall.

**Result:** met. With 1,000 blocks at 1920×1080, a whole frame takes ~8–9 ms,
GPU included. At 2560×1440 it is ~13–15 ms. In the extreme scenes the physics
budget (D-59) turns overload into a moment of slow motion instead of a stall.

## Reference setup

| Item | Value |
| ---- | ----- |
| GPU | NVIDIA GeForce RTX 3060, driver 595.99.02, Vulkan via SDL3 GPU API |
| CPU | Intel Core i3-14100F (4 cores / 8 threads), `powersave` governor |
| OS | Ubuntu 24.04, Linux 7.0.0-42-generic |
| Build | `clang-release` preset (Clang 23.1.6, `-O3`-level Release), Jolt 5.6.0 |
| Simulation | 480 Hz fixed step (D-11), 7 Jolt worker threads (hardware threads − 1) |
| Rendering | MSAA 4×, SSAO + contact shadows, DOF, bloom, photo detail, 4096² shadow map |
| Conditions | Normal desktop session (terminal, browser, IDE indexer running): the tail values (p99, max) vary from run to run; averages are stable within a few percent |

## Scenes

All performance scenes stand on the play mat (`Simulation::addPlayArea`).

| Scene | Content |
| ----- | ------- |
| `field` | N cubes as a grid of 10-cube towers (1,000 → 100 towers, 10 cm apart) |
| `collapse` | `field`; at t = 1 s, 30 % of the towers are pushed over (≈ 380 blocks move) |
| `knockall` | `field`; at t = 1 s, every tower is pushed over (all 1,000 move) |
| `pile` | N random blocks dropped at once from a 3D grid (on a large floor) |

Spawning wakes all bodies for the first ~0.5 s, so each run starts with a
short phase where everything is active.

## Commands

```bash
# Physics only (headless). Frame statistics group 8 steps (480 Hz / 60 fps) per frame.
./build/clang-release/apps/bench/woodyn_bench --scene knockall --count 1000 --seconds 6
./build/clang-release/apps/bench/woodyn_bench --scene collapse --count 1000 --timeline
./build/clang-release/apps/bench/woodyn_bench --scene knockall --threads 4 --csv

# Whole frame (physics + CPU drawing + GPU), offscreen, vsync off.
# --perf waits for the GPU every frame: "frame" = physics + draw + GPU, serialized
# (an upper bound; with vsync the CPU and GPU overlap).
./build/clang-release/apps/viewer/woodyn_viewer --scene collapse --frames 400 --hidden --mute --perf --size 1920x1080
./build/clang-release/apps/viewer/woodyn_viewer --scene field --frames 240 --hidden --mute --perf --off ssao,dof,bloom,detail
```

In the interactive viewer, the F1 HUD shows the frame time split into
physics and drawing, and "SLOW MOTION" while the physics budget is exceeded.

## 1. Where the time went (before Phase 5)

The first measurement showed the frame was **GPU bound, and the GPU was
fragment bound**:

| `field` (1,000 blocks), GPU time per frame | 320×200 | 640×400 | 1280×800 | 1920×1080 |
| --- | --- | --- | --- | --- |
| before Phase 5 | – | 5.4 ms | 13.2 ms | **22.4 ms** (misses 60 fps) |

GPU time grew with the pixel count. Turning SSAO, DOF, bloom and photo detail
off saved only ~0.8 ms. Geometry is cheap (a cube has 332 triangles; all
1,000 blocks in all passes cost ≈ 0.7 ms). The cost was the procedural wood
shader running on pixels that end up hidden:

1. Culling was off, so every hidden **back face** was shaded.
2. The MSAA main pass did not use the depth prepass, so every **overlapping
   surface** was shaded, front to back or not.

Physics was cheaper than D-11 suggested. Playing a collapse cost 1.4 ms per
frame on average, but 59 % of a *resting* field's physics time was
bookkeeping: locked per-body transform reads, 2 × 1,000 per step.

## 2. Changes and their effect

| Step | `field` GPU, 1280×800 | `field` GPU, 1920×1080 | empty room, 1280×800 |
| ---- | --- | --- | --- |
| before | 13.2 ms | 22.4 ms | 4.8 ms |
| D-56 back-face culling | 8.8 ms | 15.4 ms | 3.7 ms |
| D-57 depth prepass in the MSAA pass | **4.2 ms** | **7.5 ms** | 2.8 ms |

Image check (D-57): screenshots before and after were compared pixel by pixel.
Cases: house at 0.5 / 0.75 / 1.6 m, showcase, grain demo, placement ghost. The
maximum difference is ≤ 3/255, apart from 8 isolated silhouette pixels.

| Physics per 60 fps frame (bench, avg) | `field` | `collapse` | `knockall` |
| --- | --- | --- | --- |
| before D-58 (locked body access) | 0.57 ms | 1.36 ms | 1.68 ms |
| after D-58 (no-lock body access) | 0.37 ms | 1.15 ms | 1.73 ms |

Resting physics in the viewer went from 0.40 to 0.15 ms per frame. The
`knockall` average is within run-to-run noise.

## 3. Results (after Phase 5)

### Physics (`woodyn_bench`, 1,000 blocks, median of 3 runs)

| Scene | step avg | frame avg | frame p99 | frame max | frames > 16.7 ms |
| ----- | -------- | --------- | --------- | --------- | ---------------- |
| `field` | 0.047 ms | 0.38 ms | 3.2 ms | 5.0 ms | 0 / 360 |
| `collapse` | 0.144 ms | 1.15 ms | 4.2 ms | 5.7 ms | 0 / 360 |
| `knockall` | 0.216 ms | 1.73 ms | 8.6 ms | 10.6 ms | 0 / 360 |
| `pile` (all falling) | 1.24 ms | 9.90 ms | 16.3 ms | 23.7 ms | 3 / 360 |

`pile` is the case D-11's 9.7 ms estimate described: every block is in free
fall and colliding at once. It goes over budget in about 1 % of frames, and
D-59 handles those.

Block count (`knockall`, frame avg / p99): 250 → 0.48 / 2.0 ms ·
500 → 0.95 / 4.1 ms · 1,000 → 2.0 / 9.1 ms · 2,000 → 4.4 / 17.2 ms.
Cost is linear in the number of moving blocks.

Worker threads (`knockall`, 1,000 blocks, frame avg): 1 → 3.8 ms · 2 → 2.9 ·
3 → 2.5 · 4 → 2.2 · 5 → 2.1 · 6 → 2.0 · 7 → 2.0 ms. A 480 Hz step is only
~0.25 ms long, so splitting it into jobs stops paying off beyond the 4
physical cores. The default stays at hardware threads − 1 (D-60).

### Whole frame (`woodyn_viewer --perf`)

| Scene | Resolution | frame avg | frame p99 | GPU avg | physics avg / p99 |
| ----- | ---------- | --------- | --------- | ------- | ----------------- |
| `house` (23 blocks) | 1280×800 | 3.4 ms | 5.6 ms | 2.9 ms | 0.03 / 0.06 ms |
| `house` | 1920×1080 | 5.7 ms | 6.6 ms | 5.3 ms | 0.02 / 0.04 ms |
| `house` | 2560×1440 | 9.2 ms | 9.8 ms | 8.8 ms | 0.02 / 0.04 ms |
| `field` (1,000) | 1280×800 | 5.0 ms | 8.1 ms | 4.4 ms | 0.18 / 0.49 ms |
| `field` | 1920×1080 | 8.4 ms | 10.7 ms | 7.6 ms | 0.21 / 0.71 ms |
| `field` | 2560×1440 | 13.3 ms | 15.1 ms | 12.5 ms | 0.20 / 0.52 ms |
| `collapse` (1,000) | 1280×800 | 6.2 ms | 15.9 ms | 4.3 ms | 1.39 / 10.6 ms |
| `collapse` | 1920×1080 | 8.7 ms | 11.2 ms | 7.3 ms | 0.94 / 3.4 ms |
| `collapse` | 2560×1440 | 14.8 ms | 25.9 ms | 12.3 ms | 1.97 / 12.5 ms |

"frame" serializes CPU and GPU (worst case). The physics tail of `collapse`
varies with desktop load: of three 1080p runs, two dropped no steps and one
had 14 slow-motion frames (41 steps dropped).

## 4. Safety net: physics budget (D-59)

The viewer gives physics **10 ms** of each frame. When a frame's steps run
past it, the remaining steps of that frame are dropped. The simulation then
runs slower than real time for a moment, and the frame rate holds. Without it,
a slow frame would schedule more steps in the next frame (up to 24, the
20 fps catch-up cap), which makes that frame slower still.

## 5. Measured and left as is (D-60)

- **Frustum culling / LOD**: off-screen blocks cost well under 1 ms of GPU time.
- **Thread count**: no gain beyond the physical cores; see the sweep above.
- **480 Hz**: kept for tall towers (D-11). Play scenes need only 0.2–2 ms of
  physics per frame.
- **Textures / shaders**: no further hot spot showed up. What remains is
  resolution-bound post-processing (empty room: 2.8 ms at 1280×800).

Ideas if larger scenes or 4K become a goal: render scale or dynamic
resolution for post-processing, a cheaper SSAO at half resolution, and a
lower-resolution shadow map.
