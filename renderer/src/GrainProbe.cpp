#include "woodyn/render/GrainProbe.hpp"

#include "GpuUtil.hpp"

#include "woodyn/core/Log.hpp"

#include <glm/vec4.hpp>

#include <bit>
#include <cstring>

namespace woodyn::render {
namespace {

struct GpuProbe {
    glm::vec4 localPos;
    glm::vec4 localNormal;
    glm::vec4 cutRot;
    glm::vec4 cutCenter;
    glm::vec4 stain;
};

struct GpuWood {
    glm::vec4 pithRing;
    glm::vec4 contrastRay;
    glm::vec4 fiberSeed;
    glm::vec4 earlywood;
    glm::vec4 latewood;
};

} // namespace

std::vector<glm::vec3> evaluateGrainOnGpu(SDL_GPUDevice* device, std::span<const wood::WoodSourceParams> sources,
                                          std::span<const GrainQuery> queries, const std::string& shaderDir) {
    std::vector<glm::vec3> result;
    if (queries.empty() || sources.empty()) return result;

    const std::string code = detail::readFile(shaderDir + "/grain_probe.comp.spv");
    if (code.empty()) {
        log::error("grain_probe.comp.spv not found in {}", shaderDir);
        return result;
    }
    SDL_GPUComputePipelineCreateInfo info{};
    info.code_size = code.size();
    info.code = reinterpret_cast<const Uint8*>(code.data());
    info.entrypoint = "main";
    info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    info.num_readonly_storage_buffers = 2;
    info.num_readwrite_storage_buffers = 1;
    info.num_uniform_buffers = 1;
    info.threadcount_x = 64;
    info.threadcount_y = 1;
    info.threadcount_z = 1;
    SDL_GPUComputePipeline* pipe = SDL_CreateGPUComputePipeline(device, &info);
    if (!pipe) {
        log::error("SDL_CreateGPUComputePipeline: {}", SDL_GetError());
        return result;
    }

    std::vector<GpuProbe> probes;
    for (const auto& q : queries) {
        GpuProbe p{};
        p.localPos = glm::vec4(q.localPoint, 1.0f);
        p.localNormal = glm::vec4(q.localNormal, 0.0f);
        p.cutRot = {q.cut.rotation.x, q.cut.rotation.y, q.cut.rotation.z, q.cut.rotation.w};
        p.cutCenter = glm::vec4(q.cut.center, static_cast<float>(q.cut.source));
        p.stain = glm::vec4(materials::stainColor(q.stain), q.stain == materials::Stain::Natural ? 0.0f : 1.0f);
        probes.push_back(p);
    }
    std::vector<GpuWood> woods;
    for (const auto& s : sources) {
        woods.push_back({{s.pithOffset.x, s.pithOffset.y, s.ringWidth, s.ringWobble},
                         {s.ringContrast, s.rayDensity, s.rayDarkening, s.poreBand},
                         {s.fiberNoise, std::bit_cast<float>(s.seed), s.length, s.radius},
                         glm::vec4(s.earlywood, 1.0f), glm::vec4(s.latewood, 1.0f)});
    }

    const auto probeBytes = static_cast<std::uint32_t>(probes.size() * sizeof(GpuProbe));
    const auto woodBytes = static_cast<std::uint32_t>(woods.size() * sizeof(GpuWood));
    const auto outBytes = static_cast<std::uint32_t>(queries.size() * sizeof(glm::vec4));
    SDL_GPUBuffer* probeBuf = detail::createBuffer(device, SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ, probeBytes);
    SDL_GPUBuffer* woodBuf = detail::createBuffer(device, SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ, woodBytes);
    SDL_GPUBuffer* outBuf = detail::createBuffer(device, SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE, outBytes);
    detail::uploadBuffer(device, probeBuf, std::as_bytes(std::span(probes)));
    detail::uploadBuffer(device, woodBuf, std::as_bytes(std::span(woods)));

    SDL_GPUTransferBufferCreateInfo tinfo{};
    tinfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    tinfo.size = outBytes;
    SDL_GPUTransferBuffer* download = SDL_CreateGPUTransferBuffer(device, &tinfo);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
    const SDL_GPUStorageBufferReadWriteBinding rw{outBuf, false, 0, 0, 0};
    SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(cmd, nullptr, 0, &rw, 1);
    SDL_BindGPUComputePipeline(pass, pipe);
    SDL_GPUBuffer* ro[2] = {probeBuf, woodBuf};
    SDL_BindGPUComputeStorageBuffers(pass, 0, ro, 2);
    const std::uint32_t count[4] = {static_cast<std::uint32_t>(queries.size()), 0, 0, 0};
    SDL_PushGPUComputeUniformData(cmd, 0, count, sizeof(count));
    SDL_DispatchGPUCompute(pass, static_cast<Uint32>((queries.size() + 63) / 64), 1, 1);
    SDL_EndGPUComputePass(pass);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    const SDL_GPUBufferRegion src{outBuf, 0, outBytes};
    const SDL_GPUTransferBufferLocation dst{download, 0};
    SDL_DownloadFromGPUBuffer(copy, &src, &dst);
    SDL_EndGPUCopyPass(copy);
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    SDL_WaitForGPUFences(device, true, &fence, 1);
    SDL_ReleaseGPUFence(device, fence);

    const auto* data = static_cast<const glm::vec4*>(SDL_MapGPUTransferBuffer(device, download, false));
    for (std::size_t i = 0; i < queries.size(); ++i) result.emplace_back(data[i]);
    SDL_UnmapGPUTransferBuffer(device, download);

    SDL_ReleaseGPUTransferBuffer(device, download);
    SDL_ReleaseGPUBuffer(device, probeBuf);
    SDL_ReleaseGPUBuffer(device, woodBuf);
    SDL_ReleaseGPUBuffer(device, outBuf);
    SDL_ReleaseGPUComputePipeline(device, pipe);
    return result;
}

} // namespace woodyn::render
