#pragma once

#include "DebugRenderer.hpp"
#include "OrbitCamera.hpp"

#include "woodyn/render/GpuRenderer.hpp"
#include "woodyn/simulation/Simulation.hpp"
#include "woodyn/wood/WoodLibrary.hpp"

#include <SDL3/SDL.h>

#include <memory>
#include <vector>

namespace woodyn::viewer {

/// Draws the simulation with the GPU renderer and composites the result into
/// an SDL GPU renderer (which also draws the HUD and presents).
class GpuView {
public:
    GpuView(SDL_GPUDevice* device, SDL_Renderer* renderer, const simulation::PlayArea& area);
    ~GpuView();
    GpuView(const GpuView&) = delete;
    GpuView& operator=(const GpuView&) = delete;

    [[nodiscard]] bool ok() const;
    void draw(const simulation::Simulation& sim, const OrbitCamera& camera, const Overlay& overlay);

    render::Settings settings;

private:
    void buildDecor(const simulation::PlayArea& area);

    SDL_Renderer* m_renderer;
    std::unique_ptr<render::GpuRenderer> m_gpu;
    std::uint32_t m_plankMesh = 0;
    std::uint32_t m_railMesh = 0;
    wood::WoodLibrary m_decorWood;          // floorboards (oak) and fence (beech)
    std::vector<render::WoodInstance> m_decor; // cut.source relative to m_decorWood
    std::size_t m_sourceCount = 0;
    SDL_GPUTexture* m_wrappedGpu = nullptr;
    int m_wrappedW = 0;
    int m_wrappedH = 0;
    SDL_Texture* m_wrapped = nullptr;
};

} // namespace woodyn::viewer
