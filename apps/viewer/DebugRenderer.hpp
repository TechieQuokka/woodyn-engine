#pragma once

#include "GrainAtlas.hpp"
#include "OrbitCamera.hpp"
#include "Playroom.hpp"

#include "woodyn/geometry/RoundedMesh.hpp"
#include "woodyn/simulation/Simulation.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <memory>
#include <optional>
#include <vector>

namespace woodyn::viewer {

/// Extra things the viewer draws on top of the simulated blocks.
struct Overlay {
    std::optional<std::size_t> hovered;  // block under the cursor
    std::optional<std::size_t> grabbed;  // block held by the hand
    struct Ghost {
        blocks::BlockType type;
        physics::Transform transform;
    };
    std::optional<Ghost> ghost;          // drop preview
    std::optional<std::pair<glm::vec3, glm::vec3>> rubberBand; // grab point -> hand
};

/// Software 3D renderer on top of SDL_RenderGeometry for the physics prototype:
/// rounded meshes with per-vertex (Gouraud) lighting in linear space, planar
/// floor shadows, painter's-algorithm sorting and 2x supersampling.
/// The production Vulkan renderer replaces this in Phase 4.
class DebugRenderer {
public:
    explicit DebugRenderer(SDL_Renderer* renderer);

    /// Sets the room drawn around the blocks (playroom skin).
    void setRoom(Room room) { m_room = std::move(room); }
    ~DebugRenderer();
    DebugRenderer(const DebugRenderer&) = delete;
    DebugRenderer& operator=(const DebugRenderer&) = delete;

    /// Renders the scene into the current render target (the window).
    void draw(const simulation::Simulation& sim, const OrbitCamera& camera, const Overlay& overlay);

    /// World-space ray through a pixel of the window's render output.
    static std::pair<glm::vec3, glm::vec3> pickRay(const OrbitCamera& camera, float x, float y,
                                                   int width, int height);

private:
    struct Triangle {
        std::array<SDL_FPoint, 3> screen;
        std::array<SDL_FColor, 3> color;
        float depth;
        std::array<SDL_FPoint, 3> uv{}; // atlas UVs (textured pass only)
    };

    /// Texture source for collectBlock: grain atlas layout + block region, or none.
    struct Texturing {
        const std::vector<glm::vec2>* uv = nullptr; // per vertex, layout texels
        glm::vec2 origin{0.0f};                     // block region in the atlas
    };

    void ensureTargets(int width, int height);
    void drawBackdrop(const glm::mat4& viewProj);
    void drawShadows(const simulation::Simulation& sim, const glm::mat4& viewProj);
    void collectBlock(const geometry::RenderMesh& mesh, const Texturing& tex, const glm::mat4& model,
                      const glm::vec3& baseLinear, float highlight, const glm::mat4& viewProj,
                      const glm::vec3& eye, float alpha);
    void drawGhost(const Overlay::Ghost& ghost, const glm::mat4& viewProj, const glm::vec3& eye);
    void flush(bool sort, SDL_Texture* texture = nullptr);

    SDL_Renderer* m_renderer;
    std::unique_ptr<GrainAtlas> m_atlas; // per-block baked wood grain
    SDL_Texture* m_scene = nullptr;   // supersampled color target
    SDL_Texture* m_shadow = nullptr;  // supersampled shadow mask
    int m_targetW = 0;
    int m_targetH = 0;
    Room m_room;
    std::vector<Triangle> m_triangles;
    std::vector<SDL_Vertex> m_vertices;
};

} // namespace woodyn::viewer
