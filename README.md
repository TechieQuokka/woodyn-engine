# WOODYN — Real Wood. Real Physics.

Real-time 3D rigid-body simulator of solid-wood toy blocks (C++20, Jolt Physics,
SDL3, Vulkan later). Design: [`docs/DESIGN.md`](docs/DESIGN.md). Decisions are
logged in §15.

**Status:** Phase 1–2 done, plus toy interaction (grab-and-drag, rolling resistance,
rounded edges) and the playroom (stained colors, impact sounds, enclosed play area, undo).

## Requirements (Linux)

| Tool | Version | Path |
| ---- | ------- | ---- |
| Clang (primary) | 23 | `/usr/bin/clang++-23` |
| GCC (compatibility) | 13 | `/usr/bin/g++` |
| CMake | ≥ 3.30 (4.4.5 used) | |
| Ninja | 1.13 | `/usr/local/bin/ninja` |
| Vulkan SDK | 1.4.363.1 | `/opt/vulkan-sdk` (needed from Phase 4) |

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
# Interactive debug viewer
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
| + Shift+mouse up/down, or wheel | raise / lower |
| + Ctrl+mouse | turn it in the hand |
| + hold `Space` | gently align to a square orientation |
| + `Q` / `E` / `R` | turn ±90° / tip 90° |
| Release | drop or throw — a blue ghost previews where it lands |
| `1`–`9` | new block at the cursor (Cube, Brick, Beam, Roof, Cylinder, HalfCyl, Arch, Cone, Pyramid) |
| `C` / `O` | recolor held or hovered block / Beech↔Oak |
| `F2` `F3` `F4` `F5` / `Delete` | tower, house, showcase, pile / clear |
| `Ctrl+Z` / `Ctrl+Y` (or `Ctrl+Shift+Z`) | undo / redo |
| `P` / `M` / `F1` / `Esc` | pause / mute / help / quit |

## Layout

```text
engine/include/woodyn/   public headers (core, geometry, blocks, materials, physics, simulation)
engine/src/              implementation (Jolt stays private here)
apps/viewer/             SDL3 debug viewer (temporary, until the Vulkan renderer)
apps/bench/              headless scenario runner
tests/                   GoogleTest suites (physics validation per DESIGN.md §11)
cmake/                   dependencies and warning flags
docs/                    design document
```
