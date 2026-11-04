# WOODYN — Real Wood. Real Physics.

Real-time 3D rigid-body simulator of solid-wood toy blocks (C++20, Jolt Physics,
SDL3, Vulkan PBR renderer via the SDL3 GPU API). Design: [`docs/DESIGN.md`](docs/DESIGN.md). Decisions are
logged in §15.

**Status:** Phase 1–2 done, plus toy interaction (grab-and-drag, rolling resistance,
rounded edges), the playroom (stained colors, impact sounds, enclosed play area, undo) and
Phase 3 (log coordinates, procedural grain with continuity across cut blocks) and
Phase 4 stage 1 (GPU PBR renderer: shadows, AO, contact shadows, IBL, depth of field, bloom).

## Requirements (Linux)

| Tool | Version | Path |
| ---- | ------- | ---- |
| Clang (primary) | 23 | `/usr/bin/clang++-23` |
| GCC (compatibility) | 13 | `/usr/bin/g++` |
| CMake | ≥ 3.30 (4.4.5 used) | |
| Ninja | 1.13 | `/usr/local/bin/ninja` |
| Vulkan SDK | 1.4.363.1 | `/opt/vulkan-sdk` (glslc compiles the shaders) |

Libraries (Jolt, SDL3, GLM, spdlog, GoogleTest) are fetched and SHA-256 verified
by CMake at configure time — no system packages needed except SDL3's build
dependencies (X11/Wayland/audio `-dev` packages).

## Build & test

```bash
cmake --preset clang-debug      # or clang-release / gcc-debug
cmake --build --preset clang-debug
ctest --preset clang-debug
```

## Run

```bash
# Interactive viewer (Vulkan PBR; add --renderer soft for the software debug renderer)
./build/clang-release/apps/viewer/woodyn_viewer --scene house

# Headless physics benchmark / tuning
./build/clang-release/apps/bench/woodyn_bench --scene pile --count 1000 --seconds 10
./build/clang-release/apps/bench/woodyn_bench --scene tower --count 20 --set stepHz=600
```

Listen to the impact sounds without the viewer:

```bash
./build/clang-release/apps/bench/woodyn_bench --scene drops --seconds 8 --wav out/sounds/drops.wav
./build/clang-release/apps/bench/woodyn_bench --scene pile --count 40 --seconds 5 --wav out/sounds/pile.wav
```

Viewer controls:

| Input | Action |
| ----- | ------ |
| `W` `A` `S` `D` | move the camera |
| Right-drag · middle-drag (or Shift+right-drag) · wheel | orbit · pan · zoom |
| Left-drag a block | carry it (natural grip keeps its pose) |
| Bottom shelf | press a shape to take a new block into the hand; swatches pick the color; `Tab` hides it |
| + Shift+mouse up/down, or wheel | raise / lower |
| + `Q` / `E` (turn), `R` / `F` (tip) | tap = 1°, hold = continuous, Shift = 90° |
| + Ctrl+wheel / Ctrl+mouse / Ctrl+Shift+mouse | 1° per notch / free turn / fine turn |
| + hold `Space` | gently align to a square orientation |
| Release | drop or throw — a blue ghost previews where it lands |
| `1`–`9` | new block at the cursor (Cube, Brick, Beam, Roof, Cylinder, HalfCyl, Arch, Cone, Pyramid) |
| `C` / `O` | recolor held or hovered block / Beech↔Oak |
| `F2` `F3` `F4` `F5` `F6` / `Delete` | tower, house, showcase, pile, grain demo / clear |
| `Ctrl+Z` / `Ctrl+Y` (or `Ctrl+Shift+Z`) | undo / redo |
| `F7` / `F8` / `F9` | depth of field / ambient occlusion + contact shadows / bloom |
| `F11` | fullscreen |
| `P` / `M` / `F1` / `Esc` | pause / mute / help / quit |

## Layout

```text
engine/include/woodyn/   public headers (core, geometry, blocks, materials, wood, audio, physics, simulation)
engine/src/              implementation (Jolt stays private here)
renderer/                GPU renderer (SDL3 GPU API / Vulkan) and GLSL shaders
apps/viewer/             interactive viewer (GPU renderer, software fallback)
apps/bench/              headless scenario runner
tests/                   GoogleTest suites (physics validation per DESIGN.md §11)
cmake/                   dependencies and warning flags
docs/                    design document
```
