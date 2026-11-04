#include "woodyn/render/GrainProbe.hpp"
#include "woodyn/wood/WoodLibrary.hpp"

#include <SDL3/SDL.h>
#include <glm/geometric.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace woodyn;

namespace {

struct GpuDevice {
    SDL_GPUDevice* device = nullptr;
    GpuDevice() {
        // GPU devices need the video subsystem; offscreen works without a display.
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
        SDL_Init(SDL_INIT_VIDEO);
        device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, false, nullptr);
    }
    ~GpuDevice() {
        if (device) SDL_DestroyGPUDevice(device);
        SDL_Quit();
    }
};

} // namespace

// D-35: the shader port of surfaceAlbedo must match the CPU reference.
TEST(GpuGrainParity, ShaderMatchesCpuReference) {
    GpuDevice gpu;
    if (!gpu.device) GTEST_SKIP() << "no Vulkan GPU available: " << SDL_GetError();

    wood::WoodLibrary lib;
    std::vector<render::GrainQuery> queries;
    std::uint32_t rng = 12345u;
    auto rnd = [&rng]() {
        rng = rng * 1664525u + 1013904223u;
        return static_cast<float>(rng >> 8) / 16777216.0f;
    };
    for (int i = 0; i < 600; ++i) {
        const auto type = blocks::kAllBlockTypes[static_cast<std::size_t>(i) % blocks::kAllBlockTypes.size()];
        const auto species = (i % 3 == 0) ? materials::WoodSpecies::Oak : materials::WoodSpecies::Beech;
        const auto cut = lib.cut(type, species);
        const glm::vec3 h = blocks::definition(type).halfExtents;
        render::GrainQuery q;
        q.cut = cut;
        q.localPoint = {(rnd() * 2.0f - 1.0f) * h.x, (rnd() * 2.0f - 1.0f) * h.y, (rnd() * 2.0f - 1.0f) * h.z};
        q.localNormal = glm::normalize(glm::vec3(rnd() - 0.5f, rnd() - 0.5f, rnd() - 0.5f) + glm::vec3(0.0f, 1e-3f, 0.0f));
        q.stain = materials::kAllStains[static_cast<std::size_t>(i) % materials::kAllStains.size()];
        queries.push_back(q);
    }
    std::vector<wood::WoodSourceParams> sources;
    for (std::size_t s = 0; s < lib.sourceCount(); ++s) sources.push_back(lib.source(static_cast<std::uint32_t>(s)).params());

    const auto gpuColors = render::evaluateGrainOnGpu(gpu.device, sources, queries);
    ASSERT_EQ(gpuColors.size(), queries.size());

    int close = 0;
    float sumDiff = 0.0f;
    for (std::size_t i = 0; i < queries.size(); ++i) {
        const auto& q = queries[i];
        const glm::vec3 cpu = wood::surfaceAlbedo(lib.source(q.cut.source), q.cut, q.localPoint, q.localNormal, q.stain);
        const glm::vec3 d = glm::abs(cpu - gpuColors[i]);
        const float maxDiff = std::max({d.r, d.g, d.b});
        sumDiff += maxDiff;
        if (maxDiff < 2.0f / 255.0f) ++close;
    }
    // Rare points sit exactly on a hash-cell or ring boundary, where float
    // rounding differences between CPU and GPU may flip a ray fleck.
    EXPECT_GE(close, static_cast<int>(0.97 * static_cast<double>(queries.size())));
    EXPECT_LT(sumDiff / static_cast<float>(queries.size()), 0.003f);
}
