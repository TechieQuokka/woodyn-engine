#pragma once

#include <SDL3/SDL_gpu.h>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <array>

namespace woodyn::render::detail {

/// Image-based lighting of the playroom (D-40): an analytic radiance model of
/// the room (cream walls, oak floor, white ceiling, a bright window in the sun
/// direction) prefiltered into a GGX cubemap mip chain, plus 9 SH irradiance
/// coefficients pre-convolved for Lambert (already divided by pi).
struct Environment {
    SDL_GPUTexture* cube = nullptr;
    int mipLevels = 0;
    std::array<glm::vec4, 9> sh{};
};

/// Radiance seen in direction `dir` (linear RGB).
glm::vec3 playroomRadiance(const glm::vec3& dir, const glm::vec3& toSun);

Environment buildEnvironment(SDL_GPUDevice* device, const glm::vec3& toSun, int baseSize = 64);

} // namespace woodyn::render::detail
