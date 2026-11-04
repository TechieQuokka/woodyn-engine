#include "woodyn/render/GpuRenderer.hpp"

#include "Environment.hpp"
#include "GpuUtil.hpp"

#include "woodyn/core/Log.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <numeric>

namespace woodyn::render {
namespace {

using detail::ShaderResources;

constexpr SDL_GPUTextureFormat kHdr = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
constexpr SDL_GPUTextureFormat kDepth = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
constexpr SDL_GPUTextureFormat kLdr = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
constexpr SDL_GPUSampleCount kMsaa = SDL_GPU_SAMPLECOUNT_4;
constexpr std::uint32_t kShadowSize = 4096;
constexpr int kBloomLevels = 5;

// --- GPU-side structs (must match the GLSL declarations) ---------------------
struct GpuInstance {
    glm::mat4 model;
    glm::vec4 cutRot;
    glm::vec4 cutCenter;
    glm::vec4 stain;
    glm::vec4 misc;
};
static_assert(sizeof(GpuInstance) == 128);

struct GpuWood {
    glm::vec4 pithRing;
    glm::vec4 contrastRay;
    glm::vec4 fiberSeed;
    glm::vec4 earlywood;
    glm::vec4 latewood;
};
static_assert(sizeof(GpuWood) == 80);

struct FrameUniforms {
    glm::mat4 lightViewProj;
    glm::vec4 eye;
    glm::vec4 sunDir;
    glm::vec4 sunColor;
    glm::vec4 sh[9];
    glm::vec4 viewport;
    glm::vec4 params;
};

struct SsaoUniforms {
    glm::mat4 invViewProj;
    glm::mat4 viewProj;
    glm::vec4 eye;
    glm::vec4 sunDir;
    glm::vec4 settings;
};

struct DofUniforms {
    glm::mat4 invViewProj;
    glm::vec4 eye;
    glm::vec4 lens;
    glm::vec4 viewport;
};

struct MeshRange {
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    std::int32_t vertexOffset = 0;
};

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
};

GpuWood toGpu(const wood::WoodSourceParams& p) {
    GpuWood g{};
    g.pithRing = {p.pithOffset.x, p.pithOffset.y, p.ringWidth, p.ringWobble};
    g.contrastRay = {p.ringContrast, p.rayDensity, p.rayDarkening, p.poreBand};
    g.fiberSeed = {p.fiberNoise, std::bit_cast<float>(p.seed), p.length, p.radius};
    g.earlywood = glm::vec4(p.earlywood, 1.0f);
    g.latewood = glm::vec4(p.latewood, 1.0f);
    return g;
}

SDL_GPUColorTargetDescription colorTarget(SDL_GPUTextureFormat format, bool alphaBlend = false, bool additive = false) {
    SDL_GPUColorTargetDescription d{};
    d.format = format;
    if (alphaBlend || additive) {
        d.blend_state.enable_blend = true;
        d.blend_state.src_color_blendfactor = additive ? SDL_GPU_BLENDFACTOR_ONE : SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        d.blend_state.dst_color_blendfactor = additive ? SDL_GPU_BLENDFACTOR_ONE : SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        d.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
        d.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        d.blend_state.dst_alpha_blendfactor = additive ? SDL_GPU_BLENDFACTOR_ONE : SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        d.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    }
    return d;
}

} // namespace

struct GpuRenderer::Impl {
    SDL_GPUDevice* device;
    std::string shaderDir;
    bool valid = false;

    // Pipelines
    SDL_GPUGraphicsPipeline* woodPipe = nullptr;
    SDL_GPUGraphicsPipeline* roomPipe = nullptr;
    SDL_GPUGraphicsPipeline* ghostPipe = nullptr;
    SDL_GPUGraphicsPipeline* prepassWood = nullptr;
    SDL_GPUGraphicsPipeline* prepassRoom = nullptr;
    SDL_GPUGraphicsPipeline* shadowPipe = nullptr;
    SDL_GPUGraphicsPipeline* ssaoPipe = nullptr;
    SDL_GPUGraphicsPipeline* blurPipe = nullptr;
    SDL_GPUGraphicsPipeline* dofPipe = nullptr;
    SDL_GPUGraphicsPipeline* bloomDownPipe = nullptr;
    SDL_GPUGraphicsPipeline* bloomUpPipe = nullptr;
    SDL_GPUGraphicsPipeline* tonemapPipe = nullptr;

    // Samplers
    SDL_GPUSampler* linearClamp = nullptr;
    SDL_GPUSampler* pointClamp = nullptr;
    SDL_GPUSampler* shadowSampler = nullptr;
    SDL_GPUSampler* envSampler = nullptr;

    // Geometry
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<MeshRange> meshes;
    bool meshesDirty = false;
    SDL_GPUBuffer* vertexBuffer = nullptr;
    SDL_GPUBuffer* indexBuffer = nullptr;

    std::vector<RoomVertex> room;
    bool roomDirty = false;
    SDL_GPUBuffer* roomBuffer = nullptr;

    std::vector<GpuWood> woods;
    bool woodsDirty = false;
    SDL_GPUBuffer* woodBuffer = nullptr;

    SDL_GPUBuffer* instanceBuffer = nullptr;
    std::uint32_t instanceCapacity = 0;
    SDL_GPUTransferBuffer* instanceTransfer = nullptr;
    std::uint32_t transferCapacity = 0;

    // Lighting
    detail::Environment env;
    glm::vec3 envSunDir{0.0f};
    SDL_GPUTexture* shadowMap = nullptr;

    // Screen targets
    int width = 0;
    int height = 0;
    SDL_GPUTexture* preDepth = nullptr;
    SDL_GPUTexture* preNormal = nullptr;
    SDL_GPUTexture* aoRaw = nullptr;
    SDL_GPUTexture* aoBlur = nullptr;
    SDL_GPUTexture* msaaColor = nullptr;
    SDL_GPUTexture* msaaDepth = nullptr;
    SDL_GPUTexture* hdr = nullptr;
    SDL_GPUTexture* dofOut = nullptr;
    std::array<SDL_GPUTexture*, kBloomLevels> bloom{};
    SDL_GPUTexture* ldr = nullptr;

    explicit Impl(SDL_GPUDevice* d, std::string dir) : device(d), shaderDir(std::move(dir)) {}

    // --- setup -----------------------------------------------------------------
    SDL_GPUShader* shader(const char* name, SDL_GPUShaderStage stage, ShaderResources r) {
        return detail::loadShader(device, shaderDir, name, stage, r);
    }

    SDL_GPUGraphicsPipeline* makePipeline(SDL_GPUShader* vs, SDL_GPUShader* fs, const SDL_GPUVertexInputState& input,
                                          std::span<const SDL_GPUColorTargetDescription> targets, bool depth,
                                          bool depthWrite, SDL_GPUSampleCount samples, bool depthBias = false) {
        SDL_GPUGraphicsPipelineCreateInfo info{};
        info.vertex_shader = vs;
        info.fragment_shader = fs;
        info.vertex_input_state = input;
        info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        info.rasterizer_state.enable_depth_clip = true;
        if (depthBias) {
            info.rasterizer_state.enable_depth_bias = true;
            info.rasterizer_state.depth_bias_constant_factor = 1.5f;
            info.rasterizer_state.depth_bias_slope_factor = 2.0f;
        }
        info.multisample_state.sample_count = samples;
        info.depth_stencil_state.enable_depth_test = depth;
        info.depth_stencil_state.enable_depth_write = depthWrite;
        info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
        info.target_info.color_target_descriptions = targets.data();
        info.target_info.num_color_targets = static_cast<Uint32>(targets.size());
        info.target_info.has_depth_stencil_target = depth;
        info.target_info.depth_stencil_format = kDepth;
        SDL_GPUGraphicsPipeline* p = SDL_CreateGPUGraphicsPipeline(device, &info);
        if (!p) log::error("SDL_CreateGPUGraphicsPipeline: {}", SDL_GetError());
        return p;
    }

    bool init() {
        const ShaderResources vsInst{0, 0, 1, 1};
        const ShaderResources vsRoom{0, 0, 0, 1};
        SDL_GPUShader* woodVs = shader("wood.vert", SDL_GPU_SHADERSTAGE_VERTEX, vsInst);
        SDL_GPUShader* woodFs = shader("wood.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, {3, 0, 2, 1});
        SDL_GPUShader* roomVs = shader("room.vert", SDL_GPU_SHADERSTAGE_VERTEX, vsRoom);
        SDL_GPUShader* roomFs = shader("room.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, {3, 0, 0, 1});
        SDL_GPUShader* ghostFs = shader("ghost.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, {0, 0, 0, 1});
        SDL_GPUShader* preFs = shader("prepass.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, {});
        SDL_GPUShader* shadowVs = shader("shadow.vert", SDL_GPU_SHADERSTAGE_VERTEX, vsInst);
        SDL_GPUShader* shadowFs = shader("shadow.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, {});
        SDL_GPUShader* fullVs = shader("fullscreen.vert", SDL_GPU_SHADERSTAGE_VERTEX, {});
        SDL_GPUShader* ssaoFs = shader("ssao.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, {2, 0, 0, 1});
        SDL_GPUShader* blurFs = shader("ssao_blur.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, {2, 0, 0, 1});
        SDL_GPUShader* dofFs = shader("dof.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, {2, 0, 0, 1});
        SDL_GPUShader* downFs = shader("bloom_down.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, {1, 0, 0, 1});
        SDL_GPUShader* upFs = shader("bloom_up.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, {1, 0, 0, 1});
        SDL_GPUShader* toneFs = shader("tonemap.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, {2, 0, 0, 1});
        const std::array all{woodVs, woodFs, roomVs, roomFs, ghostFs, preFs, shadowVs, shadowFs,
                             fullVs, ssaoFs, blurFs, dofFs, downFs, upFs, toneFs};
        if (std::any_of(all.begin(), all.end(), [](SDL_GPUShader* s) { return s == nullptr; })) {
            for (auto* s : all) if (s) SDL_ReleaseGPUShader(device, s);
            return false;
        }

        // Vertex layouts.
        const SDL_GPUVertexBufferDescription meshBuffer{0, sizeof(Vertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0};
        const SDL_GPUVertexAttribute meshAttrs[2] = {
            {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0},
            {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 12}};
        const SDL_GPUVertexInputState meshInput{&meshBuffer, 1, meshAttrs, 2};
        const SDL_GPUVertexBufferDescription roomBufferDesc{0, sizeof(RoomVertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0};
        const SDL_GPUVertexAttribute roomAttrs[4] = {
            {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, static_cast<Uint32>(offsetof(RoomVertex, position))},
            {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, static_cast<Uint32>(offsetof(RoomVertex, normal))},
            {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, static_cast<Uint32>(offsetof(RoomVertex, albedoRoughness))},
            {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT, static_cast<Uint32>(offsetof(RoomVertex, material))}};
        const SDL_GPUVertexInputState roomInput{&roomBufferDesc, 1, roomAttrs, 4};
        const SDL_GPUVertexInputState noInput{nullptr, 0, nullptr, 0};

        const std::array hdrTarget{colorTarget(kHdr)};
        const std::array ghostTarget{colorTarget(kHdr, true)};
        const std::array addTarget{colorTarget(kHdr, false, true)};
        const std::array ldrTarget{colorTarget(kLdr)};
        const std::span<const SDL_GPUColorTargetDescription> none;

        woodPipe = makePipeline(woodVs, woodFs, meshInput, hdrTarget, true, true, kMsaa);
        roomPipe = makePipeline(roomVs, roomFs, roomInput, hdrTarget, true, true, kMsaa);
        ghostPipe = makePipeline(woodVs, ghostFs, meshInput, ghostTarget, true, false, kMsaa);
        prepassWood = makePipeline(woodVs, preFs, meshInput, hdrTarget, true, true, SDL_GPU_SAMPLECOUNT_1);
        prepassRoom = makePipeline(roomVs, preFs, roomInput, hdrTarget, true, true, SDL_GPU_SAMPLECOUNT_1);
        shadowPipe = makePipeline(shadowVs, shadowFs, meshInput, none, true, true, SDL_GPU_SAMPLECOUNT_1, true);
        ssaoPipe = makePipeline(fullVs, ssaoFs, noInput, hdrTarget, false, false, SDL_GPU_SAMPLECOUNT_1);
        blurPipe = makePipeline(fullVs, blurFs, noInput, hdrTarget, false, false, SDL_GPU_SAMPLECOUNT_1);
        dofPipe = makePipeline(fullVs, dofFs, noInput, hdrTarget, false, false, SDL_GPU_SAMPLECOUNT_1);
        bloomDownPipe = makePipeline(fullVs, downFs, noInput, hdrTarget, false, false, SDL_GPU_SAMPLECOUNT_1);
        bloomUpPipe = makePipeline(fullVs, upFs, noInput, addTarget, false, false, SDL_GPU_SAMPLECOUNT_1);
        tonemapPipe = makePipeline(fullVs, toneFs, noInput, ldrTarget, false, false, SDL_GPU_SAMPLECOUNT_1);
        for (auto* s : all) SDL_ReleaseGPUShader(device, s);

        const std::array pipes{woodPipe, roomPipe, ghostPipe, prepassWood, prepassRoom, shadowPipe,
                               ssaoPipe, blurPipe, dofPipe, bloomDownPipe, bloomUpPipe, tonemapPipe};
        if (std::any_of(pipes.begin(), pipes.end(), [](auto* p) { return p == nullptr; })) return false;

        // Samplers.
        SDL_GPUSamplerCreateInfo s{};
        s.min_filter = SDL_GPU_FILTER_LINEAR;
        s.mag_filter = SDL_GPU_FILTER_LINEAR;
        s.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
        s.address_mode_u = s.address_mode_v = s.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        s.max_lod = 16.0f;
        linearClamp = SDL_CreateGPUSampler(device, &s);
        envSampler = SDL_CreateGPUSampler(device, &s);
        s.min_filter = s.mag_filter = SDL_GPU_FILTER_NEAREST;
        s.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        pointClamp = SDL_CreateGPUSampler(device, &s);
        s.min_filter = s.mag_filter = SDL_GPU_FILTER_LINEAR;
        s.enable_compare = true;
        s.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
        shadowSampler = SDL_CreateGPUSampler(device, &s);

        shadowMap = detail::createTexture(device, kDepth, kShadowSize, kShadowSize,
                                          SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER);
        return linearClamp && pointClamp && shadowSampler && envSampler && shadowMap;
    }

    void releaseTargets() {
        for (SDL_GPUTexture** t : {&preDepth, &preNormal, &aoRaw, &aoBlur, &msaaColor, &msaaDepth, &hdr, &dofOut, &ldr}) {
            if (*t) SDL_ReleaseGPUTexture(device, *t);
            *t = nullptr;
        }
        for (auto& b : bloom) {
            if (b) SDL_ReleaseGPUTexture(device, b);
            b = nullptr;
        }
    }

    void ensureTargets(int w, int h) {
        if (w == width && h == height && ldr) return;
        releaseTargets();
        width = w;
        height = h;
        const auto uw = static_cast<std::uint32_t>(w);
        const auto uh = static_cast<std::uint32_t>(h);
        const auto rt = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        preDepth = detail::createTexture(device, kDepth, uw, uh, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER);
        preNormal = detail::createTexture(device, kHdr, uw, uh, rt);
        aoRaw = detail::createTexture(device, kHdr, uw, uh, rt);
        aoBlur = detail::createTexture(device, kHdr, uw, uh, rt);
        msaaColor = detail::createTexture(device, kHdr, uw, uh, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET, kMsaa);
        msaaDepth = detail::createTexture(device, kDepth, uw, uh, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET, kMsaa);
        hdr = detail::createTexture(device, kHdr, uw, uh, rt);
        dofOut = detail::createTexture(device, kHdr, uw, uh, rt);
        for (int i = 0; i < kBloomLevels; ++i) {
            bloom[static_cast<std::size_t>(i)] = detail::createTexture(
                device, kHdr, std::max(1u, uw >> (i + 1)), std::max(1u, uh >> (i + 1)), rt);
        }
        ldr = detail::createTexture(device, kLdr, uw, uh, rt);
    }

    // --- uploads -------------------------------------------------------------------
    void uploadStatic() {
        if (meshesDirty) {
            if (vertexBuffer) SDL_ReleaseGPUBuffer(device, vertexBuffer);
            if (indexBuffer) SDL_ReleaseGPUBuffer(device, indexBuffer);
            vertexBuffer = detail::createBuffer(device, SDL_GPU_BUFFERUSAGE_VERTEX, static_cast<std::uint32_t>(vertices.size() * sizeof(Vertex)));
            indexBuffer = detail::createBuffer(device, SDL_GPU_BUFFERUSAGE_INDEX, static_cast<std::uint32_t>(indices.size() * sizeof(std::uint32_t)));
            detail::uploadBuffer(device, vertexBuffer, std::as_bytes(std::span(vertices)));
            detail::uploadBuffer(device, indexBuffer, std::as_bytes(std::span(indices)));
            meshesDirty = false;
        }
        if (roomDirty && !room.empty()) {
            if (roomBuffer) SDL_ReleaseGPUBuffer(device, roomBuffer);
            roomBuffer = detail::createBuffer(device, SDL_GPU_BUFFERUSAGE_VERTEX, static_cast<std::uint32_t>(room.size() * sizeof(RoomVertex)));
            detail::uploadBuffer(device, roomBuffer, std::as_bytes(std::span(room)));
            roomDirty = false;
        }
        if (woodsDirty && !woods.empty()) {
            if (woodBuffer) SDL_ReleaseGPUBuffer(device, woodBuffer);
            woodBuffer = detail::createBuffer(device, SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
                                              static_cast<std::uint32_t>(woods.size() * sizeof(GpuWood)));
            detail::uploadBuffer(device, woodBuffer, std::as_bytes(std::span(woods)));
            woodsDirty = false;
        }
    }

    void uploadInstances(SDL_GPUCommandBuffer* cmd, std::span<const GpuInstance> data) {
        const auto bytes = static_cast<std::uint32_t>(std::max<std::size_t>(data.size(), 1) * sizeof(GpuInstance));
        if (bytes > instanceCapacity) {
            if (instanceBuffer) SDL_ReleaseGPUBuffer(device, instanceBuffer);
            instanceCapacity = std::bit_ceil(bytes);
            instanceBuffer = detail::createBuffer(device, SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ, instanceCapacity);
        }
        if (bytes > transferCapacity) {
            if (instanceTransfer) SDL_ReleaseGPUTransferBuffer(device, instanceTransfer);
            transferCapacity = std::bit_ceil(bytes);
            SDL_GPUTransferBufferCreateInfo t{};
            t.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            t.size = transferCapacity;
            instanceTransfer = SDL_CreateGPUTransferBuffer(device, &t);
        }
        if (data.empty()) return;
        void* mapped = SDL_MapGPUTransferBuffer(device, instanceTransfer, true);
        std::memcpy(mapped, data.data(), data.size() * sizeof(GpuInstance));
        SDL_UnmapGPUTransferBuffer(device, instanceTransfer);
        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
        const SDL_GPUTransferBufferLocation src{instanceTransfer, 0};
        const SDL_GPUBufferRegion dst{instanceBuffer, 0, static_cast<Uint32>(data.size() * sizeof(GpuInstance))};
        SDL_UploadToGPUBuffer(copy, &src, &dst, true);
        SDL_EndGPUCopyPass(copy);
    }

    // --- passes ----------------------------------------------------------------------
    struct Batch {
        std::uint32_t mesh;
        std::uint32_t first;
        std::uint32_t count;
    };

    void drawBatches(SDL_GPURenderPass* pass, std::span<const Batch> batches) {
        const SDL_GPUBufferBinding vb{vertexBuffer, 0};
        const SDL_GPUBufferBinding ib{indexBuffer, 0};
        SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
        SDL_BindGPUIndexBuffer(pass, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        for (const auto& b : batches) {
            const MeshRange& m = meshes[b.mesh];
            SDL_DrawGPUIndexedPrimitives(pass, m.indexCount, b.count, m.firstIndex, m.vertexOffset, b.first);
        }
    }

    void drawRoom(SDL_GPURenderPass* pass) {
        if (!roomBuffer || room.empty()) return;
        const SDL_GPUBufferBinding vb{roomBuffer, 0};
        SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
        SDL_DrawGPUPrimitives(pass, static_cast<Uint32>(room.size()), 1, 0, 0);
    }

    void fullscreen(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* target, SDL_GPUGraphicsPipeline* pipe,
                    std::span<const SDL_GPUTextureSamplerBinding> inputs, const void* uniforms, std::uint32_t size,
                    bool load = false) {
        SDL_GPUColorTargetInfo ct{};
        ct.texture = target;
        ct.load_op = load ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_DONT_CARE;
        ct.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);
        SDL_BindGPUGraphicsPipeline(pass, pipe);
        SDL_BindGPUFragmentSamplers(pass, 0, inputs.data(), static_cast<Uint32>(inputs.size()));
        SDL_PushGPUFragmentUniformData(cmd, 0, uniforms, size);
        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        SDL_EndGPURenderPass(pass);
    }
};

GpuRenderer::GpuRenderer(SDL_GPUDevice* device, std::string shaderDir)
    : m_impl(std::make_unique<Impl>(device, std::move(shaderDir))) {
    m_impl->valid = m_impl->init();
    if (m_impl->valid) log::info("GPU renderer ready ({})", SDL_GetGPUDeviceDriver(device));
}

GpuRenderer::~GpuRenderer() {
    auto& m = *m_impl;
    SDL_WaitForGPUIdle(m.device);
    m.releaseTargets();
    for (auto* p : {m.woodPipe, m.roomPipe, m.ghostPipe, m.prepassWood, m.prepassRoom, m.shadowPipe, m.ssaoPipe,
                    m.blurPipe, m.dofPipe, m.bloomDownPipe, m.bloomUpPipe, m.tonemapPipe}) {
        if (p) SDL_ReleaseGPUGraphicsPipeline(m.device, p);
    }
    for (auto* s : {m.linearClamp, m.pointClamp, m.shadowSampler, m.envSampler}) {
        if (s) SDL_ReleaseGPUSampler(m.device, s);
    }
    for (auto* b : {m.vertexBuffer, m.indexBuffer, m.roomBuffer, m.woodBuffer, m.instanceBuffer}) {
        if (b) SDL_ReleaseGPUBuffer(m.device, b);
    }
    if (m.instanceTransfer) SDL_ReleaseGPUTransferBuffer(m.device, m.instanceTransfer);
    if (m.shadowMap) SDL_ReleaseGPUTexture(m.device, m.shadowMap);
    if (m.env.cube) SDL_ReleaseGPUTexture(m.device, m.env.cube);
}

bool GpuRenderer::ok() const noexcept { return m_impl->valid; }

std::uint32_t GpuRenderer::addMesh(const MeshData& mesh) {
    auto& m = *m_impl;
    MeshRange r;
    r.firstIndex = static_cast<std::uint32_t>(m.indices.size());
    r.indexCount = static_cast<std::uint32_t>(mesh.indices.size());
    r.vertexOffset = static_cast<std::int32_t>(m.vertices.size());
    for (std::size_t i = 0; i < mesh.positions.size(); ++i) {
        m.vertices.push_back({mesh.positions[i], mesh.normals[i]});
    }
    m.indices.insert(m.indices.end(), mesh.indices.begin(), mesh.indices.end());
    m.meshes.push_back(r);
    m.meshesDirty = true;
    return static_cast<std::uint32_t>(m.meshes.size() - 1);
}

void GpuRenderer::setRoom(std::vector<RoomVertex> triangles) {
    m_impl->room = std::move(triangles);
    m_impl->roomDirty = true;
}

void GpuRenderer::setWoodSources(std::span<const wood::WoodSourceParams> sources) {
    auto& m = *m_impl;
    if (sources.size() == m.woods.size() && !m.woodsDirty && !sources.empty()) {
        return; // sources are immutable once created; only growth matters
    }
    m.woods.clear();
    for (const auto& s : sources) m.woods.push_back(toGpu(s));
    m.woodsDirty = true;
}

SDL_GPUTexture* GpuRenderer::render(int width, int height, const Camera& camera, const Sun& sun,
                                    std::span<const WoodInstance> instances, const std::optional<Ghost>& ghost,
                                    const Settings& settings) {
    auto& m = *m_impl;
    if (!m.valid || width <= 0 || height <= 0 || m.meshes.empty()) return nullptr;
    m.ensureTargets(width, height);
    if (!m.env.cube || glm::length(m.envSunDir - sun.toSun) > 1e-4f) {
        if (m.env.cube) SDL_ReleaseGPUTexture(m.device, m.env.cube);
        m.env = detail::buildEnvironment(m.device, sun.toSun);
        m.envSunDir = sun.toSun;
    }
    if (m.woods.empty()) {
        // Shaders index the wood buffer: keep a valid default entry.
        const auto p = wood::defaultParams(materials::WoodSpecies::Beech, 1);
        setWoodSources(std::span(&p, 1));
    }
    m.uploadStatic();

    // Instances sorted by mesh -> one instanced draw per mesh (+ the ghost last).
    std::vector<std::uint32_t> order(instances.size());
    std::iota(order.begin(), order.end(), 0u);
    std::stable_sort(order.begin(), order.end(),
                     [&](std::uint32_t a, std::uint32_t b) { return instances[a].mesh < instances[b].mesh; });
    std::vector<GpuInstance> gpu;
    gpu.reserve(instances.size() + 1);
    std::vector<Impl::Batch> batches;
    for (const auto idx : order) {
        const WoodInstance& w = instances[idx];
        if (w.mesh >= m.meshes.size()) continue;
        GpuInstance g{};
        g.model = w.model;
        g.cutRot = {w.cut.rotation.x, w.cut.rotation.y, w.cut.rotation.z, w.cut.rotation.w};
        g.cutCenter = glm::vec4(w.cut.center, static_cast<float>(std::min<std::size_t>(w.cut.source, m.woods.size() - 1)));
        g.stain = glm::vec4(w.dyeLinear, w.stained ? 1.0f : 0.0f);
        g.misc = {w.highlight, w.finish == Finish::FloorVarnish ? 1.0f : 0.0f, 0.0f, 0.0f};
        const auto slot = static_cast<std::uint32_t>(gpu.size());
        gpu.push_back(g);
        if (!batches.empty() && batches.back().mesh == w.mesh) ++batches.back().count;
        else batches.push_back({w.mesh, slot, 1});
    }
    std::optional<Impl::Batch> ghostBatch;
    if (ghost && ghost->mesh < m.meshes.size()) {
        GpuInstance g{};
        g.model = ghost->model;
        g.misc = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        ghostBatch = Impl::Batch{ghost->mesh, static_cast<std::uint32_t>(gpu.size()), 1};
        gpu.push_back(g);
    }

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(m.device);
    if (!cmd) {
        log::error("SDL_AcquireGPUCommandBuffer: {}", SDL_GetError());
        return nullptr;
    }
    m.uploadInstances(cmd, gpu);

    // --- matrices ----------------------------------------------------------------
    const glm::vec3 L = glm::normalize(sun.toSun);
    const glm::vec3 lightUp = std::abs(L.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
    const glm::mat4 lightView = glm::lookAt(sun.shadowCenter + L * 3.0f, sun.shadowCenter, lightUp);
    const float hs = sun.shadowHalfSize;
    const glm::mat4 lightProj = glm::ortho(-hs, hs, -hs, hs, 0.5f, 6.0f);
    const glm::mat4 lightViewProj = lightProj * lightView;
    const glm::mat4 viewProj = camera.proj * camera.view;
    const glm::mat4 invViewProj = glm::inverse(viewProj);
    const float fw = static_cast<float>(width);
    const float fh = static_cast<float>(height);

    // --- 1) sun shadow map ------------------------------------------------------------
    {
        SDL_GPUDepthStencilTargetInfo dt{};
        dt.texture = m.shadowMap;
        dt.clear_depth = 1.0f;
        dt.load_op = SDL_GPU_LOADOP_CLEAR;
        dt.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, nullptr, 0, &dt);
        SDL_BindGPUGraphicsPipeline(pass, m.shadowPipe);
        SDL_PushGPUVertexUniformData(cmd, 0, &lightViewProj, sizeof(glm::mat4));
        SDL_BindGPUVertexStorageBuffers(pass, 0, &m.instanceBuffer, 1);
        m.drawBatches(pass, batches);
        SDL_EndGPURenderPass(pass);
    }

    // --- 2) depth + normal prepass -----------------------------------------------------
    {
        SDL_GPUColorTargetInfo ct{};
        ct.texture = m.preNormal;
        ct.clear_color = {0.0f, 1.0f, 0.0f, 0.0f};
        ct.load_op = SDL_GPU_LOADOP_CLEAR;
        ct.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPUDepthStencilTargetInfo dt{};
        dt.texture = m.preDepth;
        dt.clear_depth = 1.0f;
        dt.load_op = SDL_GPU_LOADOP_CLEAR;
        dt.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ct, 1, &dt);
        SDL_PushGPUVertexUniformData(cmd, 0, &viewProj, sizeof(glm::mat4));
        SDL_BindGPUGraphicsPipeline(pass, m.prepassRoom);
        m.drawRoom(pass);
        SDL_BindGPUGraphicsPipeline(pass, m.prepassWood);
        SDL_BindGPUVertexStorageBuffers(pass, 0, &m.instanceBuffer, 1);
        m.drawBatches(pass, batches);
        SDL_EndGPURenderPass(pass);
    }

    // --- 3) SSAO + contact shadows, blur -----------------------------------------------
    {
        SsaoUniforms u{invViewProj, viewProj, glm::vec4(camera.eye, 1.0f), glm::vec4(L, 0.0f),
                       glm::vec4(0.022f, 0.012f, 1.15f, 0.006f)};
        const std::array in{SDL_GPUTextureSamplerBinding{m.preDepth, m.pointClamp},
                            SDL_GPUTextureSamplerBinding{m.preNormal, m.pointClamp}};
        m.fullscreen(cmd, m.aoRaw, m.ssaoPipe, in, &u, sizeof(u));
        const glm::vec4 blurU[2] = {glm::vec4(1.0f / fw, 1.0f / fh, 0.0f, 0.0f),
                                    glm::vec4(camera.nearZ, camera.farZ, 0.0f, 0.0f)};
        const std::array bin{SDL_GPUTextureSamplerBinding{m.aoRaw, m.linearClamp},
                             SDL_GPUTextureSamplerBinding{m.preDepth, m.pointClamp}};
        m.fullscreen(cmd, m.aoBlur, m.blurPipe, bin, blurU, sizeof(blurU));
    }

    // --- 4) main forward pass: MSAA 4x HDR -> resolve --------------------------------------
    {
        FrameUniforms f{};
        f.lightViewProj = lightViewProj;
        f.eye = glm::vec4(camera.eye, 1.0f);
        f.sunDir = glm::vec4(L, 0.0f);
        f.sunColor = glm::vec4(sun.radiance, 0.0f);
        for (int i = 0; i < 9; ++i) f.sh[i] = m.env.sh[static_cast<std::size_t>(i)];
        f.viewport = {fw, fh, 1.0f / fw, 1.0f / fh};
        f.params = {settings.ssao ? 1.0f : 0.0f, settings.contactShadows ? 1.0f : 0.0f,
                    static_cast<float>(m.env.mipLevels - 1), 2.0f * hs / static_cast<float>(kShadowSize)};

        SDL_GPUColorTargetInfo ct{};
        ct.texture = m.msaaColor;
        const glm::vec3 bg = detail::playroomRadiance(glm::vec3(0.0f, 0.2f, -1.0f), L);
        ct.clear_color = {bg.r, bg.g, bg.b, 1.0f};
        ct.load_op = SDL_GPU_LOADOP_CLEAR;
        ct.store_op = SDL_GPU_STOREOP_RESOLVE;
        ct.resolve_texture = m.hdr;
        SDL_GPUDepthStencilTargetInfo dt{};
        dt.texture = m.msaaDepth;
        dt.clear_depth = 1.0f;
        dt.load_op = SDL_GPU_LOADOP_CLEAR;
        dt.store_op = SDL_GPU_STOREOP_DONT_CARE;
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ct, 1, &dt);
        SDL_PushGPUVertexUniformData(cmd, 0, &viewProj, sizeof(glm::mat4));
        SDL_PushGPUFragmentUniformData(cmd, 0, &f, sizeof(f));
        const std::array lighting{SDL_GPUTextureSamplerBinding{m.shadowMap, m.shadowSampler},
                                  SDL_GPUTextureSamplerBinding{m.env.cube, m.envSampler},
                                  SDL_GPUTextureSamplerBinding{m.aoBlur, m.linearClamp}};

        SDL_BindGPUGraphicsPipeline(pass, m.roomPipe);
        SDL_BindGPUFragmentSamplers(pass, 0, lighting.data(), 3);
        m.drawRoom(pass);

        SDL_BindGPUGraphicsPipeline(pass, m.woodPipe);
        SDL_BindGPUFragmentSamplers(pass, 0, lighting.data(), 3);
        SDL_BindGPUVertexStorageBuffers(pass, 0, &m.instanceBuffer, 1);
        SDL_GPUBuffer* fragBuffers[2] = {m.instanceBuffer, m.woodBuffer};
        SDL_BindGPUFragmentStorageBuffers(pass, 0, fragBuffers, 2);
        m.drawBatches(pass, batches);

        if (ghostBatch) {
            const glm::vec4 ghostU[2] = {glm::vec4(camera.eye, 1.0f), glm::vec4(0.45f, 0.70f, 1.0f, 0.32f)};
            SDL_BindGPUGraphicsPipeline(pass, m.ghostPipe);
            SDL_BindGPUVertexStorageBuffers(pass, 0, &m.instanceBuffer, 1);
            SDL_PushGPUFragmentUniformData(cmd, 0, ghostU, sizeof(ghostU));
            m.drawBatches(pass, std::span(&*ghostBatch, 1));
        }
        SDL_EndGPURenderPass(pass);
    }

    // --- 5) depth of field --------------------------------------------------------------------
    {
        DofUniforms u{invViewProj, glm::vec4(camera.eye, 1.0f),
                      glm::vec4(camera.focusDistance, settings.dofCocScale, settings.dofMaxCoc, settings.dof ? 1.0f : 0.0f),
                      glm::vec4(fw, fh, 1.0f / fw, 1.0f / fh)};
        const std::array in{SDL_GPUTextureSamplerBinding{m.hdr, m.linearClamp},
                            SDL_GPUTextureSamplerBinding{m.preDepth, m.pointClamp}};
        m.fullscreen(cmd, m.dofOut, m.dofPipe, in, &u, sizeof(u));
    }

    // --- 6) bloom ----------------------------------------------------------------------------
    {
        SDL_GPUTexture* src = m.dofOut;
        int sw = width, sh = height;
        for (int i = 0; i < kBloomLevels; ++i) {
            const glm::vec4 u[2] = {glm::vec4(1.0f / static_cast<float>(sw), 1.0f / static_cast<float>(sh), 0, 0),
                                    glm::vec4(1.4f, 0.6f, i == 0 ? 1.0f : 0.0f, 0.0f)};
            const SDL_GPUTextureSamplerBinding in{src, m.linearClamp};
            m.fullscreen(cmd, m.bloom[static_cast<std::size_t>(i)], m.bloomDownPipe, std::span(&in, 1), u, sizeof(u));
            src = m.bloom[static_cast<std::size_t>(i)];
            sw = std::max(1, sw / 2);
            sh = std::max(1, sh / 2);
        }
        for (int i = kBloomLevels - 1; i > 0; --i) {
            const int lw = std::max(1, width >> (i + 1));
            const int lh = std::max(1, height >> (i + 1));
            const glm::vec4 u{1.0f / static_cast<float>(lw), 1.0f / static_cast<float>(lh), 1.0f, 0.0f};
            const SDL_GPUTextureSamplerBinding in{m.bloom[static_cast<std::size_t>(i)], m.linearClamp};
            m.fullscreen(cmd, m.bloom[static_cast<std::size_t>(i - 1)], m.bloomUpPipe, std::span(&in, 1), &u, sizeof(u), true);
        }
    }

    // --- 7) tone mapping -------------------------------------------------------------------------
    {
        const glm::vec4 u{settings.exposure, settings.bloom ? settings.bloomStrength : 0.0f, settings.vignette, 0.0f};
        const std::array in{SDL_GPUTextureSamplerBinding{m.dofOut, m.linearClamp},
                            SDL_GPUTextureSamplerBinding{m.bloom[0], m.linearClamp}};
        m.fullscreen(cmd, m.ldr, m.tonemapPipe, in, &u, sizeof(u));
    }

    SDL_SubmitGPUCommandBuffer(cmd);
    return m.ldr;
}

} // namespace woodyn::render
