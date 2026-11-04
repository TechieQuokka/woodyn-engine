#pragma once

#include "woodyn/wood/WoodCut.hpp"
#include "woodyn/wood/WoodSource.hpp"

#include <SDL3/SDL_gpu.h>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace woodyn::render {

/// Triangle mesh in object space (position + smooth normal), indexed.
struct MeshData {
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<std::uint32_t> indices;
};

/// Non-wood room surface vertex (walls, play mat). Triangle list.
struct RoomVertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec4 albedoRoughness{0.8f, 0.8f, 0.8f, 0.8f}; // linear rgb + perceptual roughness
    float material = 0.0f;                             // 0 paint, 1 EVA foam
};

enum class Finish : std::uint8_t {
    ToyLacquer,   // satin lacquer of toy blocks and the fence
    FloorVarnish, // glossy floorboards
};

/// One wooden object drawn with the procedural grain shader.
struct WoodInstance {
    std::uint32_t mesh = 0;
    glm::mat4 model{1.0f};
    wood::WoodCut cut;          // cut.source = index into setWoodSources()
    glm::vec3 dyeLinear{1.0f};  // stain dye color (materials::stainColor)
    bool stained = false;
    float highlight = 1.0f;
    Finish finish = Finish::ToyLacquer;
};

struct Ghost {
    std::uint32_t mesh = 0;
    glm::mat4 model{1.0f};
};

struct Camera {
    glm::mat4 view{1.0f};
    glm::mat4 proj{1.0f}; // clip space: +Y up (SDL GPU convention), depth 0..1
    glm::vec3 eye{0.0f};
    float nearZ = 0.005f;
    float farZ = 50.0f;
    float focusDistance = 0.75f; // [m] depth of field focus
};

struct Sun {
    glm::vec3 toSun{-0.45f, 1.0f, 0.35f}; // direction toward the light
    glm::vec3 radiance{3.2f, 3.0f, 2.7f}; // warm afternoon sun through a window
    glm::vec3 shadowCenter{0.0f};
    float shadowHalfSize = 0.9f;          // [m] shadow map covers the play area
};

struct Settings {
    bool ssao = true;
    bool contactShadows = true;
    bool bloom = true;
    bool dof = true;
    float exposure = 1.0f;
    float bloomStrength = 0.05f;
    float dofCocScale = 9.0f;  // [px] blur per unit relative defocus ("tilt-shift" strength)
    float dofMaxCoc = 6.0f;    // [px]
    float vignette = 0.18f;
};

/// Real-time PBR renderer on the SDL3 GPU API (Vulkan on Linux) — D-37..D-41.
///
/// Passes per frame: sun shadow map → depth/normal prepass → SSAO + contact
/// shadows (+ blur) → MSAA 4x HDR forward pass (procedural wood grain, clear
/// coat, IBL) → depth of field → bloom → Khronos PBR Neutral tone mapping.
class GpuRenderer {
public:
    explicit GpuRenderer(SDL_GPUDevice* device, std::string shaderDir = WOODYN_SHADER_DIR);
    ~GpuRenderer();
    GpuRenderer(const GpuRenderer&) = delete;
    GpuRenderer& operator=(const GpuRenderer&) = delete;

    [[nodiscard]] bool ok() const noexcept;

    /// Registers a mesh; returns its id for WoodInstance::mesh / Ghost::mesh.
    std::uint32_t addMesh(const MeshData& mesh);
    void setRoom(std::vector<RoomVertex> triangles);
    void setWoodSources(std::span<const wood::WoodSourceParams> sources);

    /// Renders a frame and submits it. Returns the tone-mapped RGBA8 texture
    /// (valid until the next call), usable by an SDL GPU renderer.
    SDL_GPUTexture* render(int width, int height, const Camera& camera, const Sun& sun,
                           std::span<const WoodInstance> instances, const std::optional<Ghost>& ghost,
                           const Settings& settings);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace woodyn::render
