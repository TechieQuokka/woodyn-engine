#pragma once

#include "woodyn/geometry/RoundedMesh.hpp"
#include "woodyn/simulation/Simulation.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace woodyn::viewer {

/// Per-block wood grain textures for the debug viewer (Phase 3 preview).
///
/// Each block type's (tessellated) render mesh is unwrapped once into charts
/// grouped by dominant face direction; every texel stores the block-local
/// surface point it covers. Per block, the grain is baked by evaluating
/// wood::surfaceAlbedo at those points — the grain is fixed in block space,
/// so a texture is baked once and simply moves with the block.
class GrainAtlas {
public:
    struct TypeLayout {
        geometry::RenderMesh mesh;          // tessellated, non-indexed triangles
        std::vector<glm::vec2> uv;          // per vertex, in layout texels
        int width = 0;
        int height = 0;
        std::vector<glm::vec3> texelPoint;  // block-local surface point per texel
        std::vector<glm::vec3> texelNormal;
        std::vector<std::uint8_t> texelValid;
    };

    explicit GrainAtlas(SDL_Renderer* renderer, int size = 4096, float texelSize = 0.0005f);
    ~GrainAtlas();
    GrainAtlas(const GrainAtlas&) = delete;
    GrainAtlas& operator=(const GrainAtlas&) = delete;

    const TypeLayout& layout(blocks::BlockType type);

    /// Makes sure every block has a baked texture (bakes missing ones in parallel).
    void prepare(const simulation::Simulation& sim);

    /// Atlas pixel offset of a block's texture, if baked.
    [[nodiscard]] std::optional<glm::vec2> region(const simulation::BlockInstance& block) const;

    [[nodiscard]] SDL_Texture* texture() const { return m_texture; }
    [[nodiscard]] float size() const { return static_cast<float>(m_size); }
    /// Normalized UV of a pure white texel (untextured geometry in the same batch).
    [[nodiscard]] SDL_FPoint whiteUV() const { return {2.0f / static_cast<float>(m_size), 2.0f / static_cast<float>(m_size)}; }

private:
    struct Entry {
        int x = 0;
        int y = 0;
        materials::Stain stain{};
        glm::vec3 cutCenter{0.0f};
    };

    TypeLayout buildLayout(blocks::BlockType type) const;
    std::optional<std::pair<int, int>> allocate(int w, int h);
    void reset();

    SDL_Texture* m_texture = nullptr;
    int m_size;
    float m_texel;
    std::array<std::optional<TypeLayout>, 16> m_layouts;
    std::unordered_map<std::uint32_t, Entry> m_entries; // by body id
    int m_shelfX = 0;
    int m_shelfY = 0;
    int m_shelfH = 0;
};

} // namespace woodyn::viewer
