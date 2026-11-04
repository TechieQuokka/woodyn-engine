#pragma once

// Private helpers around the SDL3 GPU API.

#include <SDL3/SDL_gpu.h>

#include <cstdint>
#include <span>
#include <string>

namespace woodyn::render::detail {

struct ShaderResources {
    std::uint32_t samplers = 0;
    std::uint32_t storageTextures = 0;
    std::uint32_t storageBuffers = 0;
    std::uint32_t uniformBuffers = 0;
};

/// Loads `<dir>/<name>.spv` (compiled by glslc at build time).
SDL_GPUShader* loadShader(SDL_GPUDevice* device, const std::string& dir, const std::string& name,
                          SDL_GPUShaderStage stage, const ShaderResources& resources);

SDL_GPUTexture* createTexture(SDL_GPUDevice* device, SDL_GPUTextureFormat format, std::uint32_t width,
                              std::uint32_t height, SDL_GPUTextureUsageFlags usage,
                              SDL_GPUSampleCount samples = SDL_GPU_SAMPLECOUNT_1, std::uint32_t levels = 1,
                              SDL_GPUTextureType type = SDL_GPU_TEXTURETYPE_2D, std::uint32_t layers = 1);

SDL_GPUBuffer* createBuffer(SDL_GPUDevice* device, SDL_GPUBufferUsageFlags usage, std::uint32_t size);

/// Uploads bytes into a GPU buffer using a temporary transfer buffer and its
/// own command buffer (for one-off uploads).
void uploadBuffer(SDL_GPUDevice* device, SDL_GPUBuffer* buffer, std::span<const std::byte> data);

/// Reads the whole file into memory.
std::string readFile(const std::string& path);

} // namespace woodyn::render::detail
