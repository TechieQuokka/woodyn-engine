# Third-party components

WOODYN's own source code is licensed under the PolyForm Noncommercial License
1.0.0 (see [LICENSE](LICENSE)). The components below are **not** part of that
license and keep their own terms.

## Libraries (downloaded at build time, not stored in this repository)

CMake `FetchContent` downloads these at configure time, pinned to exact
versions and verified by SHA-256 (see `cmake/Dependencies.cmake`).

| Component | Version | License |
| --------- | ------- | ------- |
| [Jolt Physics](https://github.com/jrouwe/JoltPhysics) | 5.6.0 | MIT |
| [SDL](https://github.com/libsdl-org/SDL) | 3.4.20 | zlib |
| [GLM](https://github.com/g-truc/glm) | 1.0.3 | MIT (Happy Bunny License also offered) |
| [spdlog](https://github.com/gabime/spdlog) | 1.17.0 | MIT |
| [GoogleTest](https://github.com/google/googletest) | 1.18.0 | BSD-3-Clause |
| [stb_image](https://github.com/nothings/stb) | commit a7d4c21 | MIT or public domain (Unlicense) |

## Assets stored in this repository

| File | Source | License |
| ---- | ------ | ------- |
| `assets/wood/fiber_detail.png` | Derived from [ambientCG Wood058](https://ambientcg.com/a/Wood058) | [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/) |

Details: [assets/wood/SOURCES.md](assets/wood/SOURCES.md).

## Tools

The Vulkan SDK (glslc), LLVM/Clang, CMake and Ninja are build tools installed
on the developer's system; they are not distributed with this project.
