#include "GrainAtlas.hpp"

#include "woodyn/core/Log.hpp"
#include "woodyn/wood/WoodCut.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <thread>
#include <tuple>

namespace woodyn::viewer {
namespace {

constexpr int kPad = 2; // texels around each chart (filtering bleed)

/// Splits triangles until no edge exceeds `maxEdge` (painter accuracy + texture affinity).
geometry::RenderMesh tessellate(const geometry::RenderMesh& in, float maxEdge) {
    geometry::RenderMesh out;
    struct V {
        glm::vec3 p;
        glm::vec3 n;
    };
    std::vector<std::array<V, 3>> stack;
    for (std::size_t i = 0; i + 2 < in.indices.size(); i += 3) {
        stack.push_back({{{in.positions[in.indices[i]], in.normals[in.indices[i]]},
                          {in.positions[in.indices[i + 1]], in.normals[in.indices[i + 1]]},
                          {in.positions[in.indices[i + 2]], in.normals[in.indices[i + 2]]}}});
        while (!stack.empty()) {
            const auto t = stack.back();
            stack.pop_back();
            std::size_t longest = 0;
            float best = 0.0f;
            for (std::size_t e = 0; e < 3; ++e) {
                const float len = glm::length(t[(e + 1) % 3].p - t[e].p);
                if (len > best) {
                    best = len;
                    longest = e;
                }
            }
            if (best <= maxEdge) {
                const auto base = static_cast<std::uint32_t>(out.positions.size());
                for (const auto& v : t) {
                    out.positions.push_back(v.p);
                    out.normals.push_back(v.n);
                }
                out.indices.insert(out.indices.end(), {base, base + 1, base + 2});
                continue;
            }
            const V& a = t[longest];
            const V& b = t[(longest + 1) % 3];
            const V& c = t[(longest + 2) % 3];
            const V m{(a.p + b.p) * 0.5f, glm::normalize(a.n + b.n)};
            stack.push_back({a, m, c});
            stack.push_back({m, b, c});
        }
    }
    return out;
}

/// The two in-plane axes used to project onto a face of dominant axis `axis`.
std::pair<int, int> planeAxes(int axis) {
    switch (axis) {
    case 0: return {2, 1};
    case 1: return {0, 2};
    default: return {0, 1};
    }
}

struct UnionFind {
    std::vector<std::size_t> parent;
    explicit UnionFind(std::size_t n) : parent(n) { std::iota(parent.begin(), parent.end(), 0); }
    std::size_t find(std::size_t x) {
        while (parent[x] != x) x = parent[x] = parent[parent[x]];
        return x;
    }
    void unite(std::size_t a, std::size_t b) { parent[find(a)] = find(b); }
};

std::uint8_t toSrgb8(float linear) {
    const float s = std::pow(std::clamp(linear, 0.0f, 1.0f), 1.0f / 2.2f);
    return static_cast<std::uint8_t>(std::lround(s * 255.0f));
}

} // namespace

GrainAtlas::GrainAtlas(SDL_Renderer* renderer, int size, float texelSize)
    : m_size(size), m_texel(texelSize) {
    m_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, size, size);
    if (!m_texture) {
        log::error("grain atlas texture: {}", SDL_GetError());
        return;
    }
    SDL_SetTextureScaleMode(m_texture, SDL_SCALEMODE_LINEAR);
    reset();
}

GrainAtlas::~GrainAtlas() {
    if (m_texture) SDL_DestroyTexture(m_texture);
}

void GrainAtlas::reset() {
    m_entries.clear();
    // Reserve an 8x8 white square at the origin for untextured geometry.
    std::vector<std::uint8_t> white(8 * 8 * 4, 255);
    const SDL_Rect r{0, 0, 8, 8};
    if (m_texture) SDL_UpdateTexture(m_texture, &r, white.data(), 8 * 4);
    m_shelfX = 8;
    m_shelfY = 0;
    m_shelfH = 8;
}

std::optional<std::pair<int, int>> GrainAtlas::allocate(int w, int h) {
    if (m_shelfX + w > m_size) {
        m_shelfX = 0;
        m_shelfY += m_shelfH;
        m_shelfH = 0;
    }
    if (m_shelfY + h > m_size || w > m_size) return std::nullopt;
    const std::pair<int, int> at{m_shelfX, m_shelfY};
    m_shelfX += w;
    m_shelfH = std::max(m_shelfH, h);
    return at;
}

const GrainAtlas::TypeLayout& GrainAtlas::layout(blocks::BlockType type) {
    auto& slot = m_layouts.at(static_cast<std::size_t>(type));
    if (!slot) slot = buildLayout(type);
    return *slot;
}

GrainAtlas::TypeLayout GrainAtlas::buildLayout(blocks::BlockType type) const {
    TypeLayout L;
    L.mesh = tessellate(blocks::definition(type).renderMesh, 0.008f);
    const auto& P = L.mesh.positions;
    const std::size_t triCount = L.mesh.indices.size() / 3;

    // 1) Dominant face direction per triangle (0..5 = +X,-X,+Y,-Y,+Z,-Z).
    std::vector<int> dir(triCount);
    for (std::size_t t = 0; t < triCount; ++t) {
        const glm::vec3 n = glm::cross(P[3 * t + 1] - P[3 * t], P[3 * t + 2] - P[3 * t]);
        const glm::vec3 a = glm::abs(n);
        const int axis = (a.x >= a.y && a.x >= a.z) ? 0 : (a.y >= a.z ? 1 : 2);
        dir[t] = axis * 2 + (n[axis] < 0.0f ? 1 : 0);
    }

    // 2) Charts: connected triangles sharing a direction (via shared positions).
    UnionFind uf(triCount);
    std::map<std::tuple<long, long, long, int>, std::size_t> owner;
    for (std::size_t t = 0; t < triCount; ++t) {
        for (int k = 0; k < 3; ++k) {
            const glm::vec3& p = P[3 * t + static_cast<std::size_t>(k)];
            const auto key = std::make_tuple(std::lround(p.x * 1e6f), std::lround(p.y * 1e6f), std::lround(p.z * 1e6f), dir[t]);
            const auto [it, inserted] = owner.emplace(key, t);
            if (!inserted) uf.unite(t, it->second);
        }
    }
    std::map<std::size_t, std::vector<std::size_t>> charts;
    for (std::size_t t = 0; t < triCount; ++t) charts[uf.find(t)].push_back(t);

    // 3) Project, measure and shelf-pack the charts.
    struct Chart {
        std::vector<std::size_t> tris;
        int axis;
        glm::vec2 lo, hi;
        int w, h, x = 0, y = 0;
    };
    std::vector<Chart> list;
    float area = 0.0f;
    for (auto& [root, tris] : charts) {
        Chart c{tris, dir[tris.front()] / 2, glm::vec2(1e9f), glm::vec2(-1e9f), 0, 0};
        const auto [ua, va] = planeAxes(c.axis);
        for (const auto t : tris) {
            for (int k = 0; k < 3; ++k) {
                const glm::vec3& p = P[3 * t + static_cast<std::size_t>(k)];
                c.lo = glm::min(c.lo, glm::vec2(p[ua], p[va]));
                c.hi = glm::max(c.hi, glm::vec2(p[ua], p[va]));
            }
        }
        c.w = static_cast<int>(std::ceil((c.hi.x - c.lo.x) / m_texel)) + 2 * kPad + 1;
        c.h = static_cast<int>(std::ceil((c.hi.y - c.lo.y) / m_texel)) + 2 * kPad + 1;
        area += static_cast<float>(c.w * c.h);
        list.push_back(std::move(c));
    }
    std::sort(list.begin(), list.end(), [](const Chart& a, const Chart& b) { return a.h > b.h; });
    int maxW = std::max(64, static_cast<int>(std::sqrt(area) * 1.25f));
    for (const auto& c : list) maxW = std::max(maxW, c.w);
    int x = 0, y = 0, shelf = 0;
    for (auto& c : list) {
        if (x + c.w > maxW) {
            x = 0;
            y += shelf;
            shelf = 0;
        }
        c.x = x;
        c.y = y;
        x += c.w;
        shelf = std::max(shelf, c.h);
    }
    L.width = maxW;
    L.height = y + shelf;

    // 4) Per-vertex UVs and rasterized position/normal maps.
    L.uv.resize(P.size());
    const auto texels = static_cast<std::size_t>(L.width) * static_cast<std::size_t>(L.height);
    L.texelPoint.assign(texels, glm::vec3(0.0f));
    L.texelNormal.assign(texels, glm::vec3(0.0f, 1.0f, 0.0f));
    L.texelValid.assign(texels, 0);
    for (const auto& c : list) {
        const auto [ua, va] = planeAxes(c.axis);
        for (const auto t : c.tris) {
            glm::vec2 uv[3];
            for (std::size_t k = 0; k < 3; ++k) {
                const glm::vec3& p = P[3 * t + k];
                uv[k] = (glm::vec2(p[ua], p[va]) - c.lo) / m_texel +
                        glm::vec2(static_cast<float>(c.x + kPad), static_cast<float>(c.y + kPad));
                L.uv[3 * t + k] = uv[k];
            }
            const glm::vec2 lo = glm::min(glm::min(uv[0], uv[1]), uv[2]);
            const glm::vec2 hi = glm::max(glm::max(uv[0], uv[1]), uv[2]);
            const float denom = (uv[1].y - uv[2].y) * (uv[0].x - uv[2].x) + (uv[2].x - uv[1].x) * (uv[0].y - uv[2].y);
            if (std::abs(denom) < 1e-12f) continue;
            for (int ty = static_cast<int>(std::floor(lo.y)); ty <= static_cast<int>(std::ceil(hi.y)); ++ty) {
                for (int tx = static_cast<int>(std::floor(lo.x)); tx <= static_cast<int>(std::ceil(hi.x)); ++tx) {
                    if (tx < 0 || ty < 0 || tx >= L.width || ty >= L.height) continue;
                    const glm::vec2 q{static_cast<float>(tx) + 0.5f, static_cast<float>(ty) + 0.5f};
                    const float w0 = ((uv[1].y - uv[2].y) * (q.x - uv[2].x) + (uv[2].x - uv[1].x) * (q.y - uv[2].y)) / denom;
                    const float w1 = ((uv[2].y - uv[0].y) * (q.x - uv[2].x) + (uv[0].x - uv[2].x) * (q.y - uv[2].y)) / denom;
                    const float w2 = 1.0f - w0 - w1;
                    if (w0 < -0.02f || w1 < -0.02f || w2 < -0.02f) continue;
                    const auto i = static_cast<std::size_t>(ty) * static_cast<std::size_t>(L.width) + static_cast<std::size_t>(tx);
                    L.texelPoint[i] = P[3 * t] * w0 + P[3 * t + 1] * w1 + P[3 * t + 2] * w2;
                    L.texelNormal[i] = glm::normalize(L.mesh.normals[3 * t] * w0 + L.mesh.normals[3 * t + 1] * w1 +
                                                      L.mesh.normals[3 * t + 2] * w2);
                    L.texelValid[i] = 1;
                }
            }
        }
    }
    // 5) Dilate charts so bilinear filtering never picks up empty texels.
    for (int pass = 0; pass < kPad; ++pass) {
        auto valid = L.texelValid;
        for (int ty = 0; ty < L.height; ++ty) {
            for (int tx = 0; tx < L.width; ++tx) {
                const auto i = static_cast<std::size_t>(ty * L.width + tx);
                if (L.texelValid[i]) continue;
                for (int d = 0; d < 8 && !valid[i]; ++d) {
                    static constexpr int dx[8] = {1, -1, 0, 0, 1, -1, 1, -1};
                    static constexpr int dy[8] = {0, 0, 1, -1, 1, -1, -1, 1};
                    const int nx = tx + dx[d], ny = ty + dy[d];
                    if (nx < 0 || ny < 0 || nx >= L.width || ny >= L.height) continue;
                    const auto j = static_cast<std::size_t>(ny * L.width + nx);
                    if (!L.texelValid[j]) continue;
                    L.texelPoint[i] = L.texelPoint[j];
                    L.texelNormal[i] = L.texelNormal[j];
                    valid[i] = 1;
                }
            }
        }
        L.texelValid = valid;
    }
    return L;
}

void GrainAtlas::prepare(const simulation::Simulation& sim) {
    if (!m_texture) return;
    struct Job {
        std::size_t block;
        int x, y;
    };
    std::vector<Job> jobs;
    bool didReset = false;
    const auto& blockList = sim.blocks();
    for (std::size_t i = 0; i < blockList.size(); ++i) {
        const auto& b = blockList[i];
        const auto it = m_entries.find(b.body.value);
        if (it != m_entries.end() && it->second.stain == b.stain &&
            glm::length(it->second.cutCenter - b.cut.center) < 1e-7f) {
            continue;
        }
        const auto& L = layout(b.type);
        auto at = allocate(L.width, L.height);
        if (!at) {
            if (didReset) continue; // more blocks than fit: the rest stay untextured
            // Atlas full: start over (all textures are re-baked on demand).
            log::info("grain atlas full, re-packing");
            didReset = true;
            reset();
            jobs.clear();
            i = static_cast<std::size_t>(-1);
            continue;
        }
        m_entries[b.body.value] = Entry{at->first, at->second, b.stain, b.cut.center};
        jobs.push_back({i, at->first, at->second});
    }
    if (jobs.empty()) return;

    // Bake in parallel: each worker takes whole blocks.
    std::vector<std::vector<std::uint8_t>> pixels(jobs.size());
    const unsigned workers = std::max(1u, std::min(std::thread::hardware_concurrency(), static_cast<unsigned>(jobs.size())));
    std::vector<std::thread> pool;
    for (unsigned w = 0; w < workers; ++w) {
        pool.emplace_back([&, w] {
            for (std::size_t j = w; j < jobs.size(); j += workers) {
                const auto& b = blockList[jobs[j].block];
                const auto& L = *m_layouts.at(static_cast<std::size_t>(b.type));
                const auto& src = sim.wood().source(b.cut.source);
                auto& out = pixels[j];
                out.assign(static_cast<std::size_t>(L.width * L.height) * 4, 0);
                for (std::size_t i = 0; i < L.texelValid.size(); ++i) {
                    glm::vec3 c{0.6f, 0.45f, 0.3f};
                    if (L.texelValid[i]) {
                        c = wood::surfaceAlbedo(src, b.cut, L.texelPoint[i], L.texelNormal[i], b.stain);
                    }
                    out[4 * i] = toSrgb8(c.r);
                    out[4 * i + 1] = toSrgb8(c.g);
                    out[4 * i + 2] = toSrgb8(c.b);
                    out[4 * i + 3] = 255;
                }
            }
        });
    }
    for (auto& t : pool) t.join();
    for (std::size_t j = 0; j < jobs.size(); ++j) {
        const auto& L = *m_layouts.at(static_cast<std::size_t>(blockList[jobs[j].block].type));
        const SDL_Rect r{jobs[j].x, jobs[j].y, L.width, L.height};
        SDL_UpdateTexture(m_texture, &r, pixels[j].data(), L.width * 4);
    }
}

std::optional<glm::vec2> GrainAtlas::region(const simulation::BlockInstance& block) const {
    const auto it = m_entries.find(block.body.value);
    if (it == m_entries.end()) return std::nullopt;
    return glm::vec2(static_cast<float>(it->second.x), static_cast<float>(it->second.y));
}

} // namespace woodyn::viewer
