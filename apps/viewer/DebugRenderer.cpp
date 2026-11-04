#include "DebugRenderer.hpp"

#include "woodyn/blocks/BlockCatalog.hpp"
#include "woodyn/materials/Stain.hpp"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace woodyn::viewer {
namespace {

constexpr int kSupersample = 2;

// Lighting (linear RGB). Warm key light from upper left, cool sky fill.
const glm::vec3 kToLight = glm::normalize(glm::vec3(-0.45f, 1.0f, 0.35f));
const glm::vec3 kLightColor{1.00f, 0.96f, 0.90f};
const glm::vec3 kSkyColor{0.44f, 0.44f, 0.46f};
const glm::vec3 kGroundColor{0.30f, 0.25f, 0.22f}; // warm bounce from the mat
constexpr float kSpecular = 0.10f;   // satin-finished wood
constexpr float kShininess = 28.0f;

float toLinear(float c) { return std::pow(c, 2.2f); }
float toSrgb(float c) { return std::pow(std::clamp(c, 0.0f, 1.0f), 1.0f / 2.2f); }
glm::vec3 toLinear(const glm::vec3& c) { return {toLinear(c.r), toLinear(c.g), toLinear(c.b)}; }

glm::vec3 speciesColor(materials::WoodSpecies species) {
    switch (species) {
    case materials::WoodSpecies::Beech: return toLinear(glm::vec3(0.87f, 0.71f, 0.53f));
    case materials::WoodSpecies::Oak:   return toLinear(glm::vec3(0.72f, 0.56f, 0.36f));
    }
    return {0.6f, 0.6f, 0.6f};
}

/// Small deterministic variation so neighbouring blocks read as separate pieces.
float blockTint(std::size_t index) {
    std::uint32_t h = static_cast<std::uint32_t>(index) * 2654435761u;
    h ^= h >> 15;
    return 0.90f + 0.18f * static_cast<float>(h % 1000u) / 1000.0f;
}

SDL_FColor shade(const glm::vec3& albedo, const glm::vec3& n, const glm::vec3& toEye,
                 float highlight, float alpha) {
    const float diffuse = std::max(0.0f, glm::dot(n, kToLight));
    const float hemi = 0.5f + 0.5f * n.y;
    const glm::vec3 ambient = glm::mix(kGroundColor, kSkyColor, hemi);
    const glm::vec3 h = glm::normalize(kToLight + toEye);
    const float spec = diffuse > 0.0f ? kSpecular * std::pow(std::max(0.0f, glm::dot(n, h)), kShininess) : 0.0f;
    glm::vec3 c = albedo * (ambient + kLightColor * (1.1f * diffuse)) + kLightColor * spec;
    c *= highlight;
    return {toSrgb(c.r), toSrgb(c.g), toSrgb(c.b), alpha};
}

struct Projected {
    SDL_FPoint screen;
    float depth;
};

std::optional<Projected> project(const glm::mat4& viewProj, const glm::vec3& p, int width, int height) {
    const glm::vec4 clip = viewProj * glm::vec4(p, 1.0f);
    if (clip.w < 1e-3f) {
        return std::nullopt;
    }
    const float invW = 1.0f / clip.w;
    return Projected{{(clip.x * invW * 0.5f + 0.5f) * static_cast<float>(width),
                      (1.0f - (clip.y * invW * 0.5f + 0.5f)) * static_cast<float>(height)},
                     clip.w};
}

glm::mat4 modelMatrix(const physics::Transform& t) {
    glm::mat4 m = glm::mat4_cast(t.rotation);
    m[3] = glm::vec4(t.position, 1.0f);
    return m;
}

} // namespace

DebugRenderer::DebugRenderer(SDL_Renderer* renderer)
    : m_renderer(renderer), m_atlas(std::make_unique<GrainAtlas>(renderer)) {}

DebugRenderer::~DebugRenderer() {
    if (m_scene) SDL_DestroyTexture(m_scene);
    if (m_shadow) SDL_DestroyTexture(m_shadow);
}

std::pair<glm::vec3, glm::vec3> DebugRenderer::pickRay(const OrbitCamera& camera, float x, float y,
                                                       int width, int height) {
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    const glm::mat4 inv = glm::inverse(camera.projection(aspect) * camera.view());
    const float nx = 2.0f * x / static_cast<float>(width) - 1.0f;
    const float ny = 1.0f - 2.0f * y / static_cast<float>(height);
    glm::vec4 a = inv * glm::vec4(nx, ny, 0.0f, 1.0f);
    glm::vec4 b = inv * glm::vec4(nx, ny, 1.0f, 1.0f);
    a /= a.w;
    b /= b.w;
    return {glm::vec3(a), glm::normalize(glm::vec3(b - a))};
}

void DebugRenderer::ensureTargets(int width, int height) {
    const int w = width * kSupersample;
    const int h = height * kSupersample;
    if (m_scene && w == m_targetW && h == m_targetH) {
        return;
    }
    if (m_scene) SDL_DestroyTexture(m_scene);
    if (m_shadow) SDL_DestroyTexture(m_shadow);
    m_scene = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, w, h);
    m_shadow = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, w, h);
    SDL_SetTextureScaleMode(m_scene, SDL_SCALEMODE_LINEAR);
    SDL_SetTextureBlendMode(m_shadow, SDL_BLENDMODE_BLEND);
    m_targetW = w;
    m_targetH = h;
}

void DebugRenderer::draw(const simulation::Simulation& sim, const OrbitCamera& camera,
                         const Overlay& overlay) {
    int width = 0;
    int height = 0;
    SDL_GetRenderOutputSize(m_renderer, &width, &height);
    if (width <= 0 || height <= 0) {
        return;
    }
    ensureTargets(width, height);
    const glm::mat4 viewProj =
        camera.projection(static_cast<float>(width) / static_cast<float>(height)) * camera.view();
    const glm::vec3 eye = camera.eye();

    SDL_SetRenderTarget(m_renderer, m_scene);
    SDL_SetRenderDrawColorFloat(m_renderer, 0.99f, 0.93f, 0.84f, 1.0f);
    SDL_RenderClear(m_renderer);
    drawBackdrop(viewProj);
    drawShadows(sim, viewProj);

    m_atlas->prepare(sim);
    m_triangles.clear();
    collectBlock(m_room.fence, {}, glm::mat4(1.0f), m_room.fenceColorLinear, 1.0f, viewProj, eye, 1.0f);
    const auto& blockList = sim.blocks();
    for (std::size_t i = 0; i < blockList.size(); ++i) {
        const auto& block = blockList[i];
        float highlight = 1.0f;
        if (overlay.grabbed == i) highlight = 1.22f;
        else if (overlay.hovered == i) highlight = 1.10f;
        const auto& layout = m_atlas->layout(block.type);
        const auto region = m_atlas->region(block);
        if (region) {
            // Albedo comes from the baked grain texture; vertices carry lighting.
            collectBlock(layout.mesh, {&layout.uv, *region}, modelMatrix(sim.interpolated(i)), glm::vec3(1.0f),
                         highlight, viewProj, eye, 1.0f);
        } else {
            const glm::vec3 albedo = materials::applyStain(speciesColor(block.species), block.stain);
            collectBlock(layout.mesh, {}, modelMatrix(sim.interpolated(i)), albedo, highlight * blockTint(i),
                         viewProj, eye, 1.0f);
        }
    }
    flush(true, m_atlas->texture());

    if (overlay.ghost) {
        drawGhost(*overlay.ghost, viewProj, eye);
    }
    if (overlay.rubberBand) {
        const auto a = project(viewProj, overlay.rubberBand->first, m_targetW, m_targetH);
        const auto b = project(viewProj, overlay.rubberBand->second, m_targetW, m_targetH);
        if (a && b) {
            SDL_SetRenderDrawColorFloat(m_renderer, 1.0f, 0.85f, 0.4f, 1.0f);
            for (int d = -1; d <= 1; ++d) {
                const float o = static_cast<float>(d);
                SDL_RenderLine(m_renderer, a->screen.x + o, a->screen.y, b->screen.x + o, b->screen.y);
                SDL_RenderLine(m_renderer, a->screen.x, a->screen.y + o, b->screen.x, b->screen.y + o);
            }
            const SDL_FRect dot{b->screen.x - 4.0f, b->screen.y - 4.0f, 8.0f, 8.0f};
            SDL_RenderFillRect(m_renderer, &dot);
        }
    }

    // Downsample to the window (bilinear 2x -> box filter).
    SDL_SetRenderTarget(m_renderer, nullptr);
    SDL_RenderTexture(m_renderer, m_scene, nullptr, nullptr);
}

void DebugRenderer::drawBackdrop(const glm::mat4& viewProj) {
    // Walls, floorboards and mat are flat and drawn back to front by construction.
    m_triangles.clear();
    for (const auto& tri : m_room.backdrop) {
        Triangle t{};
        bool ok = true;
        for (std::size_t j = 0; j < 3; ++j) {
            const auto p = project(viewProj, tri.p[j], m_targetW, m_targetH);
            if (!p) {
                ok = false;
                break;
            }
            t.screen[j] = p->screen;
            t.color[j] = {toSrgb(tri.color[j].r), toSrgb(tri.color[j].g), toSrgb(tri.color[j].b), 1.0f};
        }
        if (ok) m_triangles.push_back(t);
    }
    flush(false);
}

void DebugRenderer::drawShadows(const simulation::Simulation& sim, const glm::mat4& viewProj) {
    // Project every light-facing triangle onto the floor along the light
    // direction into a mask, then darken the floor once (no double darkening).
    SDL_SetRenderTarget(m_renderer, m_shadow);
    SDL_SetRenderDrawColorFloat(m_renderer, 0.0f, 0.0f, 0.0f, 0.0f);
    SDL_RenderClear(m_renderer);

    m_triangles.clear();
    const SDL_FColor ink{0.0f, 0.0f, 0.0f, 1.0f};
    const auto& blockList = sim.blocks();
    for (std::size_t i = 0; i < blockList.size(); ++i) {
        const auto& mesh = blocks::definition(blockList[i].type).renderMesh;
        const glm::mat4 model = modelMatrix(sim.interpolated(i));
        for (std::size_t k = 0; k + 2 < mesh.indices.size(); k += 3) {
            std::array<SDL_FPoint, 3> s{};
            bool ok = true;
            glm::vec3 world[3];
            for (int j = 0; j < 3; ++j) {
                world[j] = glm::vec3(model * glm::vec4(mesh.positions[mesh.indices[k + static_cast<std::size_t>(j)]], 1.0f));
            }
            if (glm::dot(glm::cross(world[1] - world[0], world[2] - world[0]), kToLight) <= 0.0f) {
                continue;
            }
            for (int j = 0; j < 3; ++j) {
                const glm::vec3 onFloor = world[j] - kToLight * (std::max(world[j].y, 0.0f) / kToLight.y);
                const auto p = project(viewProj, onFloor, m_targetW, m_targetH);
                if (!p) {
                    ok = false;
                    break;
                }
                s[static_cast<std::size_t>(j)] = p->screen;
            }
            if (ok) m_triangles.push_back({s, {ink, ink, ink}, 0.0f});
        }
    }
    flush(false);

    SDL_SetRenderTarget(m_renderer, m_scene);
    SDL_SetTextureAlphaModFloat(m_shadow, 0.42f);
    SDL_RenderTexture(m_renderer, m_shadow, nullptr, nullptr);
}

void DebugRenderer::collectBlock(const geometry::RenderMesh& mesh, const Texturing& tex, const glm::mat4& model,
                                 const glm::vec3& baseLinear, float highlight,
                                 const glm::mat4& viewProj, const glm::vec3& eye, float alpha) {
    const glm::mat3 normalMat(model);
    for (std::size_t k = 0; k + 2 < mesh.indices.size(); k += 3) {
        glm::vec3 w[3];
        glm::vec3 n[3];
        for (int j = 0; j < 3; ++j) {
            const auto vi = mesh.indices[k + static_cast<std::size_t>(j)];
            w[j] = glm::vec3(model * glm::vec4(mesh.positions[vi], 1.0f));
            n[j] = normalMat * mesh.normals[vi];
        }
        const glm::vec3 faceN = glm::cross(w[1] - w[0], w[2] - w[0]);
        if (glm::dot(faceN, eye - w[0]) <= 0.0f) {
            continue; // back face
        }
        Triangle tri{};
        float depth = 0.0f;
        bool ok = true;
        for (int j = 0; j < 3; ++j) {
            const auto p = project(viewProj, w[j], m_targetW, m_targetH);
            if (!p) {
                ok = false;
                break;
            }
            tri.screen[static_cast<std::size_t>(j)] = p->screen;
            tri.color[static_cast<std::size_t>(j)] =
                shade(baseLinear, n[j], glm::normalize(eye - w[j]), highlight, alpha);
            if (tex.uv) {
                const glm::vec2 t = ((*tex.uv)[mesh.indices[k + static_cast<std::size_t>(j)]] + tex.origin) / m_atlas->size();
                tri.uv[static_cast<std::size_t>(j)] = {t.x, t.y};
            } else {
                tri.uv[static_cast<std::size_t>(j)] = m_atlas->whiteUV();
            }
            depth += p->depth;
        }
        if (!ok) continue;
        tri.depth = depth / 3.0f;
        m_triangles.push_back(tri);
    }
}

void DebugRenderer::drawGhost(const Overlay::Ghost& ghost, const glm::mat4& viewProj,
                              const glm::vec3& eye) {
    m_triangles.clear();
    collectBlock(m_atlas->layout(ghost.type).mesh, {}, modelMatrix(ghost.transform),
                 glm::vec3(0.55f, 0.75f, 1.0f), 1.0f, viewProj, eye, 0.28f);
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    flush(true);
}

void DebugRenderer::flush(bool sort, SDL_Texture* texture) {
    if (sort) {
        // Painter's algorithm: far to near.
        std::sort(m_triangles.begin(), m_triangles.end(),
                  [](const Triangle& l, const Triangle& r) { return l.depth > r.depth; });
    }
    m_vertices.clear();
    m_vertices.reserve(m_triangles.size() * 3);
    for (const auto& tri : m_triangles) {
        for (std::size_t j = 0; j < 3; ++j) {
            m_vertices.push_back(SDL_Vertex{tri.screen[j], tri.color[j], texture ? tri.uv[j] : SDL_FPoint{0.0f, 0.0f}});
        }
    }
    if (!m_vertices.empty()) {
        SDL_RenderGeometry(m_renderer, texture, m_vertices.data(), static_cast<int>(m_vertices.size()),
                           nullptr, 0);
    }
}

} // namespace woodyn::viewer
