#include "GpuUtil.hpp"

#include "woodyn/core/Log.hpp"

#include <cstring>
#include <fstream>
#include <sstream>

namespace woodyn::render::detail {

std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

SDL_GPUShader* loadShader(SDL_GPUDevice* device, const std::string& dir, const std::string& name,
                          SDL_GPUShaderStage stage, const ShaderResources& r) {
    const std::string path = dir + "/" + name + ".spv";
    const std::string code = readFile(path);
    if (code.empty()) {
        log::error("shader not found: {}", path);
        return nullptr;
    }
    SDL_GPUShaderCreateInfo info{};
    info.code_size = code.size();
    info.code = reinterpret_cast<const Uint8*>(code.data());
    info.entrypoint = "main";
    info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    info.stage = stage;
    info.num_samplers = r.samplers;
    info.num_storage_textures = r.storageTextures;
    info.num_storage_buffers = r.storageBuffers;
    info.num_uniform_buffers = r.uniformBuffers;
    SDL_GPUShader* shader = SDL_CreateGPUShader(device, &info);
    if (!shader) log::error("SDL_CreateGPUShader({}): {}", name, SDL_GetError());
    return shader;
}

SDL_GPUTexture* createTexture(SDL_GPUDevice* device, SDL_GPUTextureFormat format, std::uint32_t width,
                              std::uint32_t height, SDL_GPUTextureUsageFlags usage, SDL_GPUSampleCount samples,
                              std::uint32_t levels, SDL_GPUTextureType type, std::uint32_t layers) {
    SDL_GPUTextureCreateInfo info{};
    info.type = type;
    info.format = format;
    info.usage = usage;
    info.width = width;
    info.height = height;
    info.layer_count_or_depth = layers;
    info.num_levels = levels;
    info.sample_count = samples;
    SDL_GPUTexture* t = SDL_CreateGPUTexture(device, &info);
    if (!t) log::error("SDL_CreateGPUTexture {}x{}: {}", width, height, SDL_GetError());
    return t;
}

SDL_GPUBuffer* createBuffer(SDL_GPUDevice* device, SDL_GPUBufferUsageFlags usage, std::uint32_t size) {
    SDL_GPUBufferCreateInfo info{};
    info.usage = usage;
    info.size = size;
    SDL_GPUBuffer* b = SDL_CreateGPUBuffer(device, &info);
    if (!b) log::error("SDL_CreateGPUBuffer({} bytes): {}", size, SDL_GetError());
    return b;
}

void uploadBuffer(SDL_GPUDevice* device, SDL_GPUBuffer* buffer, std::span<const std::byte> data) {
    if (data.empty()) return;
    SDL_GPUTransferBufferCreateInfo tinfo{};
    tinfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tinfo.size = static_cast<Uint32>(data.size());
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &tinfo);
    void* mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
    std::memcpy(mapped, data.data(), data.size());
    SDL_UnmapGPUTransferBuffer(device, transfer);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    const SDL_GPUTransferBufferLocation src{transfer, 0};
    const SDL_GPUBufferRegion dst{buffer, 0, static_cast<Uint32>(data.size())};
    SDL_UploadToGPUBuffer(copy, &src, &dst, false);
    SDL_EndGPUCopyPass(copy);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(device, transfer);
}

} // namespace woodyn::render::detail
