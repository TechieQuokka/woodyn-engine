#pragma once

#include "woodyn/materials/Stain.hpp"
#include "woodyn/wood/WoodCut.hpp"
#include "woodyn/wood/WoodSource.hpp"

#include <SDL3/SDL_gpu.h>

#include <glm/vec3.hpp>

#include <span>
#include <string>
#include <vector>

namespace woodyn::render {

/// One point to evaluate: a block-local surface point of a cut block.
struct GrainQuery {
    glm::vec3 localPoint{0.0f};
    glm::vec3 localNormal{0.0f, 1.0f, 0.0f};
    wood::WoodCut cut;
    materials::Stain stain = materials::Stain::Natural;
};

/// Evaluates the GPU port of wood::surfaceAlbedo with a compute shader, so the
/// shader can be checked against the CPU reference (D-35).
std::vector<glm::vec3> evaluateGrainOnGpu(SDL_GPUDevice* device, std::span<const wood::WoodSourceParams> sources,
                                          std::span<const GrainQuery> queries,
                                          const std::string& shaderDir = WOODYN_SHADER_DIR);

} // namespace woodyn::render
