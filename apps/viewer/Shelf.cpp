#include "Shelf.hpp"

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <string>

namespace woodyn::viewer {
namespace {

float toSrgb(float c) { return std::pow(std::clamp(c, 0.0f, 1.0f), 1.0f / 2.2f); }

SDL_FColor stainedColor(materials::Stain stain, materials::WoodSpecies species, float shade) {
    const glm::vec3 wood = species == materials::WoodSpecies::Oak
                               ? glm::vec3(std::pow(0.72f, 2.2f), std::pow(0.56f, 2.2f), std::pow(0.36f, 2.2f))
                               : materials::stainColor(materials::Stain::Natural);
    const glm::vec3 c = materials::applyStain(wood, stain) * shade;
    return {toSrgb(c.r), toSrgb(c.g), toSrgb(c.b), 1.0f};
}

bool inside(const SDL_FRect& r, float x, float y) {
    return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
}

/// Filled rounded rectangle from triangles.
void roundedRect(SDL_Renderer* r, const SDL_FRect& rect, float radius, SDL_FColor color) {
    std::vector<SDL_Vertex> v;
    const SDL_FPoint c{rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f};
    constexpr int kSeg = 6;
    std::vector<SDL_FPoint> outline;
    const std::array<SDL_FPoint, 4> corners{{{rect.x + rect.w - radius, rect.y + radius},
                                             {rect.x + radius, rect.y + radius},
                                             {rect.x + radius, rect.y + rect.h - radius},
                                             {rect.x + rect.w - radius, rect.y + rect.h - radius}}};
    for (int k = 0; k < 4; ++k) {
        for (int i = 0; i <= kSeg; ++i) {
            const float a = (static_cast<float>(k) + static_cast<float>(i) / kSeg) * 1.5707963f;
            outline.push_back({corners[static_cast<std::size_t>(k)].x + std::cos(a) * radius,
                               corners[static_cast<std::size_t>(k)].y - std::sin(a) * radius});
        }
    }
    for (std::size_t i = 0; i < outline.size(); ++i) {
        v.push_back({c, color, {0, 0}});
        v.push_back({outline[i], color, {0, 0}});
        v.push_back({outline[(i + 1) % outline.size()], color, {0, 0}});
    }
    SDL_RenderGeometry(r, nullptr, v.data(), static_cast<int>(v.size()), nullptr, 0);
}

void disc(SDL_Renderer* r, SDL_FPoint c, float radius, SDL_FColor color) {
    std::vector<SDL_Vertex> v;
    constexpr int kSeg = 20;
    for (int i = 0; i < kSeg; ++i) {
        const float a0 = 6.2831853f * static_cast<float>(i) / kSeg;
        const float a1 = 6.2831853f * static_cast<float>(i + 1) / kSeg;
        v.push_back({c, color, {0, 0}});
        v.push_back({{c.x + std::cos(a0) * radius, c.y + std::sin(a0) * radius}, color, {0, 0}});
        v.push_back({{c.x + std::cos(a1) * radius, c.y + std::sin(a1) * radius}, color, {0, 0}});
    }
    SDL_RenderGeometry(r, nullptr, v.data(), static_cast<int>(v.size()), nullptr, 0);
}

} // namespace

Shelf::Shelf() {
    for (std::size_t i = 0; i < blocks::kAllBlockTypes.size(); ++i) {
        m_icons[i] = buildIcon(blocks::kAllBlockTypes[i]);
    }
}

std::vector<Shelf::IconTriangle> Shelf::buildIcon(blocks::BlockType type) {
    // Fixed 3/4 view, orthographic, flat-shaded, painter-sorted (icons only).
    const auto& mesh = blocks::definition(type).renderMesh;
    const glm::quat view = glm::angleAxis(0.42f, glm::vec3(1, 0, 0)) * glm::angleAxis(-0.62f, glm::vec3(0, 1, 0));
    const glm::vec3 light = glm::normalize(glm::vec3(-0.4f, 0.9f, 0.6f));
    struct Tri {
        glm::vec3 p[3];
        float shade[3];
        float depth;
    };
    std::vector<Tri> tris;
    glm::vec2 lo{1e9f}, hi{-1e9f};
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        Tri t{};
        for (int k = 0; k < 3; ++k) {
            const auto vi = mesh.indices[i + static_cast<std::size_t>(k)];
            t.p[k] = view * mesh.positions[vi];
            const glm::vec3 n = view * mesh.normals[vi];
            t.shade[k] = 0.55f + 0.5f * std::max(0.0f, glm::dot(n, view * light));
            lo = glm::min(lo, glm::vec2(t.p[k].x, -t.p[k].y));
            hi = glm::max(hi, glm::vec2(t.p[k].x, -t.p[k].y));
        }
        const glm::vec3 fn = glm::cross(t.p[1] - t.p[0], t.p[2] - t.p[0]);
        if (fn.z <= 0.0f) continue; // facing away from the viewer (+Z)
        t.depth = (t.p[0].z + t.p[1].z + t.p[2].z) / 3.0f;
        tris.push_back(t);
    }
    std::sort(tris.begin(), tris.end(), [](const Tri& a, const Tri& b) { return a.depth < b.depth; });
    const float size = std::max(hi.x - lo.x, hi.y - lo.y);
    const glm::vec2 center = (lo + hi) * 0.5f;
    std::vector<IconTriangle> out;
    for (const auto& t : tris) {
        IconTriangle it{};
        for (int k = 0; k < 3; ++k) {
            const glm::vec2 q = (glm::vec2(t.p[k].x, -t.p[k].y) - center) / size + 0.5f;
            it.p[static_cast<std::size_t>(k)] = {q.x, q.y};
            it.shade[static_cast<std::size_t>(k)] = t.shade[k];
        }
        out.push_back(it);
    }
    return out;
}

void Shelf::layout(int width, int height) {
    m_scale = std::max(1.0f, static_cast<float>(height) / 800.0f);
    const float slot = 64.0f * m_scale;
    const float gap = 8.0f * m_scale;
    const float swatch = 22.0f * m_scale;
    const float columns = std::ceil(static_cast<float>(materials::kAllStains.size()) / 2.0f);
    const float swatchArea = columns * (swatch + gap * 0.5f) + gap;
    const float w = static_cast<float>(blocks::kAllBlockTypes.size()) * (slot + gap) + gap + swatchArea + gap;
    const float h = slot + 2.0f * gap;
    m_panel = {(static_cast<float>(width) - w) * 0.5f, static_cast<float>(height) - h - 12.0f * m_scale, w, h};
    for (std::size_t i = 0; i < m_slots.size(); ++i) {
        m_slots[i] = {m_panel.x + gap + static_cast<float>(i) * (slot + gap), m_panel.y + gap, slot, slot};
    }
    // Swatches: two rows to the right of the blocks.
    const float sx = m_slots.back().x + slot + 2.0f * gap;
    for (std::size_t i = 0; i < m_swatches.size(); ++i) {
        const float col = static_cast<float>(i / 2u);
        const float row = static_cast<float>(i % 2u);
        m_swatches[i] = {sx + col * (swatch + gap * 0.5f), m_panel.y + gap + row * (swatch + gap), swatch, swatch};
    }
}

bool Shelf::contains(float x, float y) const { return visible && inside(m_panel, x, y); }

std::optional<Shelf::Hit> Shelf::hitTest(float x, float y) const {
    if (!contains(x, y)) return std::nullopt;
    for (std::size_t i = 0; i < m_slots.size(); ++i) {
        if (inside(m_slots[i], x, y)) return BlockSlot{blocks::kAllBlockTypes[i]};
    }
    for (std::size_t i = 0; i < m_swatches.size(); ++i) {
        if (inside(m_swatches[i], x, y)) return StainSwatch{materials::kAllStains[i]};
    }
    return std::nullopt;
}

void Shelf::draw(SDL_Renderer* r, materials::Stain nextStain, materials::WoodSpecies species, float mx,
                 float my) const {
    if (!visible) return;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    // Wooden shelf board.
    roundedRect(r, {m_panel.x, m_panel.y + 3.0f * m_scale, m_panel.w, m_panel.h}, 14.0f * m_scale, {0.25f, 0.17f, 0.10f, 0.35f});
    roundedRect(r, m_panel, 14.0f * m_scale, {0.93f, 0.82f, 0.66f, 0.92f});

    std::optional<std::size_t> hovered;
    for (std::size_t i = 0; i < m_slots.size(); ++i) {
        const SDL_FRect& s = m_slots[i];
        const bool hover = inside(s, mx, my);
        if (hover) hovered = i;
        roundedRect(r, s, 10.0f * m_scale, hover ? SDL_FColor{1.0f, 0.97f, 0.90f, 1.0f} : SDL_FColor{0.98f, 0.92f, 0.82f, 1.0f});
        std::vector<SDL_Vertex> v;
        const float pad = 8.0f * m_scale;
        const float lift = hover ? -2.0f * m_scale : 0.0f;
        for (const auto& t : m_icons[i]) {
            for (std::size_t k = 0; k < 3; ++k) {
                v.push_back({{s.x + pad + t.p[k].x * (s.w - 2.0f * pad), s.y + pad + lift + t.p[k].y * (s.h - 2.0f * pad)},
                             stainedColor(nextStain, species, t.shade[k]), {0, 0}});
            }
        }
        SDL_RenderGeometry(r, nullptr, v.data(), static_cast<int>(v.size()), nullptr, 0);
        // Key hint (1-9) in the corner.
        SDL_SetRenderDrawColorFloat(r, 0.45f, 0.33f, 0.22f, 1.0f);
        const std::string key = std::to_string(i + 1);
        SDL_RenderDebugText(r, s.x + 4.0f * m_scale, s.y + 4.0f * m_scale, key.c_str());
    }

    for (std::size_t i = 0; i < m_swatches.size(); ++i) {
        const SDL_FRect& s = m_swatches[i];
        const SDL_FPoint c{s.x + s.w * 0.5f, s.y + s.h * 0.5f};
        const bool selected = materials::kAllStains[i] == nextStain;
        if (selected || inside(s, mx, my)) disc(r, c, s.w * 0.5f + 2.5f * m_scale, selected ? SDL_FColor{0.35f, 0.25f, 0.15f, 1.0f} : SDL_FColor{1, 1, 1, 0.9f});
        disc(r, c, s.w * 0.5f, stainedColor(materials::kAllStains[i], species, 1.0f));
    }

    if (hovered) {
        const std::string name(blocks::toString(blocks::kAllBlockTypes[*hovered]));
        const SDL_FRect& s = m_slots[*hovered];
        SDL_SetRenderDrawColorFloat(r, 0.30f, 0.22f, 0.14f, 1.0f);
        SDL_RenderDebugText(r, s.x + (s.w - 8.0f * static_cast<float>(name.size())) * 0.5f, m_panel.y - 12.0f * m_scale, name.c_str());
    }
}

} // namespace woodyn::viewer
