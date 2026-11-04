#include "Environment.hpp"

#include "GpuUtil.hpp"

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace woodyn::render::detail {
namespace {

glm::vec3 lin(float r, float g, float b) { return {std::pow(r, 2.2f), std::pow(g, 2.2f), std::pow(b, 2.2f)}; }

/// Cube face texel -> direction (Vulkan/GL cube conventions, u right, v down).
glm::vec3 faceDirection(int face, float u, float v) {
    switch (face) {
    case 0: return glm::normalize(glm::vec3(1.0f, -v, -u));
    case 1: return glm::normalize(glm::vec3(-1.0f, -v, u));
    case 2: return glm::normalize(glm::vec3(u, 1.0f, v));
    case 3: return glm::normalize(glm::vec3(u, -1.0f, -v));
    case 4: return glm::normalize(glm::vec3(u, -v, 1.0f));
    default: return glm::normalize(glm::vec3(-u, -v, -1.0f));
    }
}

float radicalInverse(std::uint32_t bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return static_cast<float>(bits) * 2.3283064365386963e-10f;
}

/// GGX-prefiltered radiance around direction n (N = V = R approximation).
glm::vec3 prefilter(const glm::vec3& n, float roughness, const glm::vec3& toSun, int samples) {
    if (roughness < 1e-3f) return playroomRadiance(n, toSun);
    const glm::vec3 up = std::abs(n.y) < 0.999f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    const glm::vec3 tx = glm::normalize(glm::cross(up, n));
    const glm::vec3 ty = glm::cross(n, tx);
    const float a = roughness * roughness;
    glm::vec3 sum{0.0f};
    float weight = 0.0f;
    for (int i = 0; i < samples; ++i) {
        const float u1 = (static_cast<float>(i) + 0.5f) / static_cast<float>(samples);
        const float u2 = radicalInverse(static_cast<std::uint32_t>(i));
        const float phi = glm::two_pi<float>() * u2;
        const float cosT = std::sqrt((1.0f - u1) / (1.0f + (a * a - 1.0f) * u1));
        const float sinT = std::sqrt(1.0f - cosT * cosT);
        const glm::vec3 h = tx * (sinT * std::cos(phi)) + ty * (sinT * std::sin(phi)) + n * cosT;
        const glm::vec3 l = 2.0f * glm::dot(n, h) * h - n;
        const float nol = glm::dot(n, l);
        if (nol <= 0.0f) continue;
        sum += playroomRadiance(l, toSun) * nol;
        weight += nol;
    }
    return weight > 0.0f ? sum / weight : playroomRadiance(n, toSun);
}

} // namespace

glm::vec3 playroomRadiance(const glm::vec3& dRaw, const glm::vec3& toSunRaw) {
    const glm::vec3 d = glm::normalize(dRaw);
    const glm::vec3 toSun = glm::normalize(toSunRaw);
    // Window: a bright, slightly blue opening around the sun direction.
    const glm::vec3 windowDir = glm::normalize(glm::vec3(toSun.x, 0.45f, toSun.z) * glm::vec3(1.6f, 1.0f, 1.6f));
    const float w = glm::dot(d, windowDir);
    if (w > 0.94f) {
        const float edge = std::clamp((w - 0.94f) / 0.02f, 0.0f, 1.0f);
        return glm::mix(lin(0.98f, 0.93f, 0.84f) * 0.9f, glm::vec3(5.5f, 5.8f, 6.4f), edge);
    }
    if (d.y < -0.05f) {
        // Oak floorboards lit by the window light.
        return lin(0.72f, 0.56f, 0.40f) * 0.55f;
    }
    if (d.y > 0.75f) {
        return lin(0.98f, 0.97f, 0.95f) * 1.05f; // white ceiling
    }
    // Walls: cream above, peach below, brighter on the side the sun lights.
    const float t = std::clamp((d.y + 0.05f) / 0.8f, 0.0f, 1.0f);
    const glm::vec3 wall = glm::mix(lin(0.98f, 0.84f, 0.78f), lin(0.99f, 0.93f, 0.84f), t);
    const float lit = 0.55f + 0.35f * std::max(0.0f, -glm::dot(glm::vec3(d.x, 0.0f, d.z), glm::vec3(toSun.x, 0.0f, toSun.z)));
    return wall * lit;
}

Environment buildEnvironment(SDL_GPUDevice* device, const glm::vec3& toSun, int baseSize) {
    Environment env;
    env.mipLevels = static_cast<int>(std::log2(static_cast<float>(baseSize))) + 1;
    env.cube = createTexture(device, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT, static_cast<std::uint32_t>(baseSize),
                             static_cast<std::uint32_t>(baseSize), SDL_GPU_TEXTUREUSAGE_SAMPLER, SDL_GPU_SAMPLECOUNT_1,
                             static_cast<std::uint32_t>(env.mipLevels), SDL_GPU_TEXTURETYPE_CUBE, 6);
    if (!env.cube) return env;

    // Prefiltered mips (roughness = mip / (levels - 1)) packed into one upload.
    struct Region {
        int face, mip, size;
        std::size_t offset;
    };
    std::vector<Region> regions;
    std::vector<std::uint16_t> data;
    for (int mip = 0; mip < env.mipLevels; ++mip) {
        const int size = std::max(1, baseSize >> mip);
        const float roughness = static_cast<float>(mip) / static_cast<float>(env.mipLevels - 1);
        const int samples = mip == 0 ? 1 : 96;
        for (int face = 0; face < 6; ++face) {
            regions.push_back({face, mip, size, data.size() * sizeof(std::uint16_t)});
            for (int y = 0; y < size; ++y) {
                for (int x = 0; x < size; ++x) {
                    const float u = 2.0f * (static_cast<float>(x) + 0.5f) / static_cast<float>(size) - 1.0f;
                    const float v = 2.0f * (static_cast<float>(y) + 0.5f) / static_cast<float>(size) - 1.0f;
                    const glm::vec3 c = prefilter(faceDirection(face, u, v), roughness, toSun, samples);
                    data.push_back(glm::packHalf1x16(c.r));
                    data.push_back(glm::packHalf1x16(c.g));
                    data.push_back(glm::packHalf1x16(c.b));
                    data.push_back(glm::packHalf1x16(1.0f));
                }
            }
        }
    }

    SDL_GPUTransferBufferCreateInfo tinfo{};
    tinfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tinfo.size = static_cast<Uint32>(data.size() * sizeof(std::uint16_t));
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &tinfo);
    std::memcpy(SDL_MapGPUTransferBuffer(device, transfer, false), data.data(), tinfo.size);
    SDL_UnmapGPUTransferBuffer(device, transfer);
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    for (const auto& r : regions) {
        SDL_GPUTextureTransferInfo src{};
        src.transfer_buffer = transfer;
        src.offset = static_cast<Uint32>(r.offset);
        src.pixels_per_row = static_cast<Uint32>(r.size);
        src.rows_per_layer = static_cast<Uint32>(r.size);
        SDL_GPUTextureRegion dst{};
        dst.texture = env.cube;
        dst.mip_level = static_cast<Uint32>(r.mip);
        dst.layer = static_cast<Uint32>(r.face);
        dst.w = static_cast<Uint32>(r.size);
        dst.h = static_cast<Uint32>(r.size);
        dst.d = 1;
        SDL_UploadToGPUTexture(copy, &src, &dst, false);
    }
    SDL_EndGPUCopyPass(copy);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(device, transfer);

    // SH9 projection of the radiance (Monte Carlo over a Fibonacci sphere).
    std::array<glm::vec3, 9> L{};
    constexpr int kDirs = 4096;
    for (int i = 0; i < kDirs; ++i) {
        const float z = 1.0f - 2.0f * (static_cast<float>(i) + 0.5f) / kDirs;
        const float r = std::sqrt(1.0f - z * z);
        const float phi = static_cast<float>(i) * 2.39996323f;
        const glm::vec3 d{r * std::cos(phi), r * std::sin(phi), z};
        const glm::vec3 c = playroomRadiance(d, toSun);
        const float basis[9] = {0.282095f, 0.488603f * d.y, 0.488603f * d.z, 0.488603f * d.x,
                                1.092548f * d.x * d.y, 1.092548f * d.y * d.z, 0.315392f * (3.0f * d.z * d.z - 1.0f),
                                1.092548f * d.x * d.z, 0.546274f * (d.x * d.x - d.y * d.y)};
        for (int k = 0; k < 9; ++k) L[static_cast<std::size_t>(k)] += c * basis[k];
    }
    const float norm = 4.0f * glm::pi<float>() / kDirs;
    // Irradiance / pi with the Ramamoorthi-Hanrahan band factors and basis constants.
    const float A[3] = {1.0f, 2.0f / 3.0f, 0.25f};
    const float C[9] = {0.282095f, 0.488603f, 0.488603f, 0.488603f, 1.092548f, 1.092548f, 0.315392f, 1.092548f, 0.546274f};
    const int band[9] = {0, 1, 1, 1, 2, 2, 2, 2, 2};
    for (int k = 0; k < 9; ++k) {
        env.sh[static_cast<std::size_t>(k)] = glm::vec4(L[static_cast<std::size_t>(k)] * norm * A[band[k]] * C[k], 0.0f);
    }
    return env;
}

} // namespace woodyn::render::detail
