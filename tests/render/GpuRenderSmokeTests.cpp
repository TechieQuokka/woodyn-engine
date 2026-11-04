#include "woodyn/blocks/BlockCatalog.hpp"
#include "woodyn/render/GpuRenderer.hpp"
#include "woodyn/wood/WoodLibrary.hpp"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <vector>

using namespace woodyn;

namespace {

struct GpuDevice {
    SDL_GPUDevice* device = nullptr;
    GpuDevice() {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
        SDL_Init(SDL_INIT_VIDEO);
        device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, false, nullptr);
    }
    ~GpuDevice() {
        if (device) SDL_DestroyGPUDevice(device);
        SDL_Quit();
    }
};

std::vector<std::uint8_t> download(SDL_GPUDevice* device, SDL_GPUTexture* tex, int w, int h) {
    const auto bytes = static_cast<Uint32>(w * h * 4);
    SDL_GPUTransferBufferCreateInfo t{};
    t.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    t.size = bytes;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(device, &t);
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion src{};
    src.texture = tex;
    src.w = static_cast<Uint32>(w);
    src.h = static_cast<Uint32>(h);
    src.d = 1;
    SDL_GPUTextureTransferInfo dst{};
    dst.transfer_buffer = tb;
    SDL_DownloadFromGPUTexture(copy, &src, &dst);
    SDL_EndGPUCopyPass(copy);
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    SDL_WaitForGPUFences(device, true, &fence, 1);
    SDL_ReleaseGPUFence(device, fence);
    std::vector<std::uint8_t> out(bytes);
    std::memcpy(out.data(), SDL_MapGPUTransferBuffer(device, tb, false), bytes);
    SDL_UnmapGPUTransferBuffer(device, tb);
    SDL_ReleaseGPUTransferBuffer(device, tb);
    return out;
}

} // namespace

// Renders a stained arch on a mat offscreen and checks the picture makes sense.
TEST(GpuRenderSmoke, RendersALitStainedBlock) {
    GpuDevice gpu;
    if (!gpu.device) GTEST_SKIP() << "no Vulkan GPU available: " << SDL_GetError();

    render::GpuRenderer renderer(gpu.device);
    ASSERT_TRUE(renderer.ok());
    const auto& arch = blocks::definition(blocks::BlockType::Arch).renderMesh;
    const auto mesh = renderer.addMesh({arch.positions, arch.normals, arch.indices});
    std::vector<render::RoomVertex> room;
    const glm::vec4 mat(0.6f, 0.75f, 0.9f, 0.9f);
    for (const glm::vec3 p : {glm::vec3(-1, 0, -1), glm::vec3(-1, 0, 1), glm::vec3(1, 0, 1),
                              glm::vec3(-1, 0, -1), glm::vec3(1, 0, 1), glm::vec3(1, 0, -1)}) {
        room.push_back({p, {0, 1, 0}, mat, 1.0f});
    }
    renderer.setRoom(room);
    wood::WoodLibrary lib;
    const auto cut = lib.cut(blocks::BlockType::Arch, materials::WoodSpecies::Beech);
    std::vector<wood::WoodSourceParams> sources{lib.source(0).params()};
    renderer.setWoodSources(sources);

    render::WoodInstance inst;
    inst.mesh = mesh;
    inst.model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.03f, 0.0f));
    inst.cut = cut;
    inst.dyeLinear = materials::stainColor(materials::Stain::Red);
    inst.stained = true;

    const int w = 320, h = 200;
    render::Camera cam;
    cam.eye = {0.0f, 0.12f, 0.25f};
    cam.view = glm::lookAt(cam.eye, glm::vec3(0.0f, 0.03f, 0.0f), glm::vec3(0, 1, 0));
    cam.proj = glm::perspective(0.75f, static_cast<float>(w) / static_cast<float>(h), cam.nearZ, cam.farZ);
    cam.focusDistance = glm::length(cam.eye - glm::vec3(0.0f, 0.03f, 0.0f));
    std::vector<render::WoodInstance> instances{inst};
    SDL_GPUTexture* image = renderer.render(w, h, cam, render::Sun{}, instances, std::nullopt, render::Settings{});
    ASSERT_NE(image, nullptr);
    const auto px = download(gpu.device, image, w, h);

    // The center of the frame shows the red-stained arch: red dominates.
    const auto at = [&](int x, int y) { return &px[static_cast<std::size_t>((y * w + x) * 4)]; };
    const std::uint8_t* c = at(w / 2, h / 2 - 10);
    EXPECT_GT(c[0], c[1] + 30) << "expected red stain at the center";
    EXPECT_GT(c[0], c[2] + 30);
    // The bottom corner shows the bluish mat, lit (not black, not blown out).
    const std::uint8_t* m = at(10, h - 10);
    EXPECT_GT(m[2], m[0]);
    EXPECT_GT(m[2], 60);
    EXPECT_LT(m[2], 255);
    // Something has real detail: the image is not flat.
    double mean = 0.0, var = 0.0;
    for (int i = 0; i < w * h; ++i) mean += px[static_cast<std::size_t>(i * 4)];
    mean /= w * h;
    for (int i = 0; i < w * h; ++i) {
        const double d = px[static_cast<std::size_t>(i * 4)] - mean;
        var += d * d;
    }
    EXPECT_GT(std::sqrt(var / (w * h)), 10.0);
}
