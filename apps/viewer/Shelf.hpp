#pragma once

#include "woodyn/blocks/BlockCatalog.hpp"
#include "woodyn/materials/Stain.hpp"
#include "woodyn/materials/WoodSpecies.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <optional>
#include <variant>
#include <vector>

namespace woodyn::viewer {

/// Toy shelf at the bottom of the screen (D-43): one slot per block shape,
/// drawn as a small shaded icon in the color the next block will get, plus
/// color swatches. Press a slot to take a new block into the hand.
class Shelf {
public:
    Shelf();

    struct BlockSlot {
        blocks::BlockType type;
    };
    struct StainSwatch {
        materials::Stain stain;
    };
    using Hit = std::variant<BlockSlot, StainSwatch>;

    /// Lays the shelf out for a render output of `width` x `height` pixels.
    void layout(int width, int height);

    [[nodiscard]] bool contains(float x, float y) const;
    [[nodiscard]] std::optional<Hit> hitTest(float x, float y) const;

    void draw(SDL_Renderer* renderer, materials::Stain nextStain, materials::WoodSpecies species,
              float mouseX, float mouseY) const;

    bool visible = true;

private:
    struct IconTriangle {
        std::array<SDL_FPoint, 3> p; // normalized to [0, 1]^2
        std::array<float, 3> shade;
    };

    static std::vector<IconTriangle> buildIcon(blocks::BlockType type);

    std::array<std::vector<IconTriangle>, blocks::kAllBlockTypes.size()> m_icons;
    SDL_FRect m_panel{};
    std::array<SDL_FRect, blocks::kAllBlockTypes.size()> m_slots{};
    std::array<SDL_FRect, materials::kAllStains.size()> m_swatches{};
    float m_scale = 1.0f;
};

} // namespace woodyn::viewer
