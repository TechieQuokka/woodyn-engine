#include "GpuView.hpp"

#include "Playroom.hpp"

#include "woodyn/core/Log.hpp"
#include "woodyn/materials/Stain.hpp"

#include <glm/gtc/quaternion.hpp>

#include <array>
#include <cmath>

namespace woodyn::viewer {
namespace {

render::MeshData toMeshData(const geometry::RenderMesh& m) {
    return {m.positions, m.normals, m.indices};
}

/// Rounded box with the given full size, built as a filleted extrusion along Z.
render::MeshData roundedBox(const glm::vec3& size, float radius) {
    const geometry::RoundingOptions r{radius, 3, 30.0f};
    const std::array<glm::vec2, 4> rect{{{-size.x * 0.5f, -size.y * 0.5f}, {size.x * 0.5f, -size.y * 0.5f},
                                         {size.x * 0.5f, size.y * 0.5f}, {-size.x * 0.5f, size.y * 0.5f}}};
    return toMeshData(geometry::roundedExtrusion(geometry::filletProfile(rect, r), size.z, r));
}

glm::mat4 modelMatrix(const physics::Transform& t) {
    glm::mat4 m = glm::mat4_cast(t.rotation);
    m[3] = glm::vec4(t.position, 1.0f);
    return m;
}

constexpr glm::vec3 kPlank{0.36f, 0.02f, 0.12f};
constexpr float kRailThickness = 0.03f;

} // namespace

GpuView::GpuView(SDL_GPUDevice* device, SDL_Renderer* renderer, const simulation::PlayArea& area)
    : m_renderer(renderer), m_gpu(std::make_unique<render::GpuRenderer>(device)) {
    if (!m_gpu->ok()) return;
    // Mesh ids 0..8 are the block types, in BlockType order.
    for (const auto type : blocks::kAllBlockTypes) {
        m_gpu->addMesh(toMeshData(blocks::definition(type).renderMesh));
    }
    m_plankMesh = m_gpu->addMesh(roundedBox(kPlank, 0.0012f));
    m_railMesh = m_gpu->addMesh(roundedBox({2.0f * (area.halfSize + kRailThickness), area.fenceHeight, kRailThickness}, 0.005f));
    m_gpu->setRoom(buildGpuRoom(Skin{}, area));
    buildDecor(area);
}

GpuView::~GpuView() {
    if (m_wrapped) SDL_DestroyTexture(m_wrapped);
}

bool GpuView::ok() const { return m_gpu && m_gpu->ok(); }

void GpuView::buildDecor(const simulation::PlayArea& area) {
    // Oak floorboards with staggered joints, under and around the mat.
    const float R = 2.4f;
    const float y = -kMatThickness - kPlank.y * 0.5f;
    int row = 0;
    for (float z = -R + kPlank.z * 0.5f; z < R; z += kPlank.z, ++row) {
        const float offset = static_cast<float>(row % 3) * kPlank.x / 3.0f;
        for (float x = -R - offset + kPlank.x * 0.5f; x < R + kPlank.x; x += kPlank.x) {
            render::WoodInstance w;
            w.mesh = m_plankMesh;
            w.model = glm::translate(glm::mat4(1.0f), glm::vec3(x, y, z));
            w.cut = m_decorWood.cutBox(kPlank * 0.5f, materials::WoodSpecies::Oak, 0);
            w.finish = render::Finish::FloorVarnish;
            m_decor.push_back(w);
        }
    }
    // Beech fence rails along the four sides of the mat.
    const glm::vec3 railHalf{area.halfSize + kRailThickness, area.fenceHeight * 0.5f, kRailThickness * 0.5f};
    for (int side = 0; side < 4; ++side) {
        const float angle = glm::half_pi<float>() * static_cast<float>(side);
        const glm::vec3 offset = glm::vec3(std::sin(angle), 0.0f, std::cos(angle)) * (area.halfSize + kRailThickness * 0.5f);
        render::WoodInstance w;
        w.mesh = m_railMesh;
        w.model = glm::rotate(glm::translate(glm::mat4(1.0f), offset + glm::vec3(0.0f, area.fenceHeight * 0.5f, 0.0f)),
                              angle, glm::vec3(0, 1, 0));
        w.cut = m_decorWood.cutBox(railHalf, materials::WoodSpecies::Beech, 0);
        m_decor.push_back(w);
    }
}

void GpuView::draw(const simulation::Simulation& sim, const OrbitCamera& camera, const Overlay& overlay) {
    int w = 0, h = 0;
    SDL_GetCurrentRenderOutputSize(m_renderer, &w, &h);
    if (w <= 0 || h <= 0 || !ok()) return;

    // Wood sources: simulation logs first, then the decor logs.
    const std::size_t simSources = sim.wood().sourceCount();
    const std::size_t total = simSources + m_decorWood.sourceCount();
    if (total != m_sourceCount) {
        std::vector<wood::WoodSourceParams> params;
        for (std::size_t i = 0; i < simSources; ++i) params.push_back(sim.wood().source(static_cast<std::uint32_t>(i)).params());
        for (std::size_t i = 0; i < m_decorWood.sourceCount(); ++i) params.push_back(m_decorWood.source(static_cast<std::uint32_t>(i)).params());
        m_gpu->setWoodSources(params);
        m_sourceCount = total;
    }

    std::vector<render::WoodInstance> instances;
    instances.reserve(sim.blocks().size() + m_decor.size());
    const auto& blockList = sim.blocks();
    for (std::size_t i = 0; i < blockList.size(); ++i) {
        const auto& b = blockList[i];
        render::WoodInstance inst;
        inst.mesh = static_cast<std::uint32_t>(b.type);
        inst.model = modelMatrix(sim.interpolated(i));
        inst.cut = b.cut;
        inst.dyeLinear = materials::stainColor(b.stain);
        inst.stained = b.stain != materials::Stain::Natural;
        inst.highlight = overlay.grabbed == i ? 1.18f : (overlay.hovered == i ? 1.08f : 1.0f);
        instances.push_back(inst);
    }
    for (auto d : m_decor) {
        d.cut.source += static_cast<std::uint32_t>(simSources);
        instances.push_back(d);
    }

    std::optional<render::Ghost> ghost;
    if (overlay.ghost) ghost = render::Ghost{static_cast<std::uint32_t>(overlay.ghost->type), modelMatrix(overlay.ghost->transform)};

    render::Camera cam;
    const float aspect = static_cast<float>(w) / static_cast<float>(h);
    cam.view = camera.view();
    cam.proj = camera.projection(aspect); // SDL GPU normalizes NDC to +Y up on every backend
    cam.eye = camera.eye();
    cam.nearZ = camera.nearPlane;
    cam.farZ = camera.farPlane;
    cam.focusDistance = camera.distance;

    SDL_GPUTexture* image = m_gpu->render(w, h, cam, render::Sun{}, instances, ghost, settings);
    if (!image) return;
    // Re-wrap when the renderer recreated its target (new pointer or new size:
    // a resized texture may reuse the old address).
    if (image != m_wrappedGpu || w != m_wrappedW || h != m_wrappedH) {
        if (m_wrapped) SDL_DestroyTexture(m_wrapped);
        SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetPointerProperty(props, SDL_PROP_TEXTURE_CREATE_GPU_TEXTURE_POINTER, image);
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_RGBA32);
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, w);
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, h);
        m_wrapped = SDL_CreateTextureWithProperties(m_renderer, props);
        SDL_DestroyProperties(props);
        m_wrappedGpu = image;
        m_wrappedW = w;
        m_wrappedH = h;
        if (!m_wrapped) log::error("wrapping GPU image failed: {}", SDL_GetError());
    }
    if (m_wrapped) SDL_RenderTexture(m_renderer, m_wrapped, nullptr, nullptr);

    if (overlay.rubberBand) {
        const glm::mat4 vp = camera.projection(aspect) * camera.view();
        auto toScreen = [&](const glm::vec3& p) {
            const glm::vec4 c = vp * glm::vec4(p, 1.0f);
            return glm::vec2((c.x / c.w * 0.5f + 0.5f) * static_cast<float>(w), (1.0f - (c.y / c.w * 0.5f + 0.5f)) * static_cast<float>(h));
        };
        const glm::vec2 a = toScreen(overlay.rubberBand->first);
        const glm::vec2 b = toScreen(overlay.rubberBand->second);
        SDL_SetRenderDrawColorFloat(m_renderer, 1.0f, 0.85f, 0.4f, 1.0f);
        SDL_RenderLine(m_renderer, a.x, a.y, b.x, b.y);
        const SDL_FRect dot{b.x - 3.0f, b.y - 3.0f, 6.0f, 6.0f};
        SDL_RenderFillRect(m_renderer, &dot);
    }
}

} // namespace woodyn::viewer
