#include "Playroom.hpp"

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>

#include <array>
#include <cmath>
#include <cstdint>

namespace woodyn::viewer {
namespace {

glm::vec3 lin(const glm::vec3& c) { return {std::pow(c.r, 2.2f), std::pow(c.g, 2.2f), std::pow(c.b, 2.2f)}; }

const glm::vec3 kToLight = glm::normalize(glm::vec3(-0.45f, 1.0f, 0.35f));

/// Simple diffuse + ambient lighting for static room surfaces (linear).
glm::vec3 lit(const glm::vec3& albedoLinear, const glm::vec3& n) {
    const float diffuse = std::max(0.0f, glm::dot(n, kToLight));
    return albedoLinear * (0.55f + 0.6f * diffuse);
}

void quad(std::vector<RoomTriangle>& out, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
          const glm::vec3& d, const glm::vec3& ca, const glm::vec3& cb, const glm::vec3& cc,
          const glm::vec3& cd) {
    out.push_back({{a, b, c}, {ca, cb, cc}});
    out.push_back({{a, c, d}, {ca, cc, cd}});
}

std::uint32_t hash(int x, int y) {
    std::uint32_t h = static_cast<std::uint32_t>(x * 73856093) ^ static_cast<std::uint32_t>(y * 19349663);
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    return h ^ (h >> 15);
}

/// One wall, tessellated so partially visible walls are not culled away.
void wall(std::vector<RoomTriangle>& out, const Skin& skin, const glm::vec3& origin, const glm::vec3& along,
          const glm::vec3& normal, float width, float height) {
    constexpr float kStep = 0.24f;
    const int nx = static_cast<int>(std::ceil(width / kStep));
    const int ny = static_cast<int>(std::ceil(height / kStep));
    const glm::vec3 up{0.0f, 1.0f, 0.0f};
    auto color = [&](float y) {
        const float t = std::clamp(y / height, 0.0f, 1.0f);
        const glm::vec3 c = y < 0.12f ? skin.baseboard : glm::mix(skin.wallBottom, skin.wallTop, t);
        return lit(lin(c), normal);
    };
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            const float x0 = static_cast<float>(i) * width / static_cast<float>(nx);
            const float x1 = static_cast<float>(i + 1) * width / static_cast<float>(nx);
            const float y0 = static_cast<float>(j) * height / static_cast<float>(ny);
            const float y1 = static_cast<float>(j + 1) * height / static_cast<float>(ny);
            quad(out, origin + along * x0 + up * y0, origin + along * x1 + up * y0,
                 origin + along * x1 + up * y1, origin + along * x0 + up * y1, color(y0), color(y0),
                 color(y1), color(y1));
        }
    }
    // Pastel polka dots: small discs slightly in front of the wall.
    const glm::vec3 inward = normal * 0.002f;
    for (int i = 1; i < static_cast<int>(width / 0.42f); ++i) {
        for (int j = 1; j < 5; ++j) {
            const std::uint32_t h = hash(i, j + static_cast<int>(origin.x * 10.0f + origin.z * 7.0f));
            const glm::vec3 c = lit(lin(skin.dots[h % skin.dots.size()]), normal);
            const float cx = static_cast<float>(i) * 0.42f + ((j % 2) ? 0.21f : 0.0f);
            if (cx > width - 0.1f) continue;
            const float cy = 0.35f + static_cast<float>(j) * 0.36f;
            const float r = 0.045f + 0.02f * static_cast<float>(h % 3);
            const glm::vec3 center = origin + along * cx + up * cy + inward;
            constexpr int kSeg = 16;
            for (int k = 0; k < kSeg; ++k) {
                const float a0 = glm::two_pi<float>() * static_cast<float>(k) / kSeg;
                const float a1 = glm::two_pi<float>() * static_cast<float>(k + 1) / kSeg;
                out.push_back({{center, center + (along * std::cos(a0) + up * std::sin(a0)) * r,
                                center + (along * std::cos(a1) + up * std::sin(a1)) * r},
                               {c, c, c}});
            }
        }
    }
}

} // namespace

Room buildPlayroom(const Skin& skin, const simulation::PlayArea* area) {
    Room room;
    const float R = room.roomHalf;
    constexpr float kWallHeight = 2.4f;

    // Walls (facing inward).
    wall(room.backdrop, skin, {-R, 0.0f, -R}, {1, 0, 0}, {0, 0, 1}, 2.0f * R, kWallHeight);
    wall(room.backdrop, skin, {R, 0.0f, R}, {-1, 0, 0}, {0, 0, -1}, 2.0f * R, kWallHeight);
    wall(room.backdrop, skin, {R, 0.0f, -R}, {0, 0, 1}, {-1, 0, 0}, 2.0f * R, kWallHeight);
    wall(room.backdrop, skin, {-R, 0.0f, R}, {0, 0, -1}, {1, 0, 0}, 2.0f * R, kWallHeight);

    // Floorboards: 12 cm planks along X with staggered joints.
    const glm::vec3 up{0.0f, 1.0f, 0.0f};
    constexpr float kPlank = 0.12f;
    constexpr float kPiece = 0.36f;
    const float y = -0.001f;
    for (int row = 0; static_cast<float>(row) * kPlank < 2.0f * R; ++row) {
        const float z0 = -R + static_cast<float>(row) * kPlank;
        for (int col = 0; static_cast<float>(col) * kPiece < 2.0f * R; ++col) {
            const float x0 = -R + static_cast<float>(col) * kPiece;
            const std::uint32_t h = hash(row, col);
            const float shade = 0.94f + 0.08f * static_cast<float>(h % 100) / 100.0f;
            const glm::vec3 c = lit(lin((row % 2 ? skin.floorA : skin.floorB) * shade), up);
            const float x1 = std::min(x0 + kPiece, R);
            const float z1 = std::min(z0 + kPlank, R);
            quad(room.backdrop, {x0, y, z0}, {x1, y, z0}, {x1, y, z1}, {x0, y, z1}, c, c, c, c);
        }
    }

    if (area) {
        // EVA foam play mat: interlocking 30 cm tiles in four pastel colors,
        // each split into a 3 x 3 grid so tiles near the camera are not culled.
        const float h = area->halfSize;
        const int tiles = std::max(1, static_cast<int>(std::round(2.0f * h / 0.3f)));
        const float size = 2.0f * h / static_cast<float>(tiles);
        for (int ix = 0; ix < tiles; ++ix) {
            for (int iz = 0; iz < tiles; ++iz) {
                const glm::vec3 c = lit(lin(skin.matTiles[static_cast<std::size_t>((ix + 2 * iz) % 4)]), up);
                for (int sx = 0; sx < 3; ++sx) {
                    for (int sz = 0; sz < 3; ++sz) {
                        const float x0 = -h + static_cast<float>(ix) * size + static_cast<float>(sx) * size / 3.0f;
                        const float z0 = -h + static_cast<float>(iz) * size + static_cast<float>(sz) * size / 3.0f;
                        const float x1 = x0 + size / 3.0f;
                        const float z1 = z0 + size / 3.0f;
                        quad(room.backdrop, {x0, 0.0f, z0}, {x1, 0.0f, z0}, {x1, 0.0f, z1}, {x0, 0.0f, z1},
                             c, c, c, c);
                    }
                }
            }
        }

        // Fence: four rounded wooden rails standing just outside the mat edge.
        const float t = 0.03f;
        const geometry::RoundingOptions rounding{0.006f, 3, 30.0f};
        const float len = 2.0f * (h + t);
        const std::array<glm::vec2, 4> profile{{{-t * 0.5f, 0.0f}, {t * 0.5f, 0.0f},
                                                {t * 0.5f, area->fenceHeight}, {-t * 0.5f, area->fenceHeight}}};
        const auto rail = geometry::roundedExtrusion(geometry::filletProfile(profile, rounding), len, rounding);
        for (int side = 0; side < 4; ++side) {
            const float angle = glm::half_pi<float>() * static_cast<float>(side);
            const float c = std::cos(angle), s = std::sin(angle);
            geometry::RenderMesh m = rail;
            for (std::size_t i = 0; i < m.positions.size(); ++i) {
                // Rail profile is in XY, length along Z; place at x = h + t/2, then rotate about Y.
                glm::vec3 p = m.positions[i] + glm::vec3(h + t * 0.5f, 0.0f, 0.0f);
                glm::vec3 n = m.normals[i];
                m.positions[i] = {c * p.x + s * p.z, p.y, -s * p.x + c * p.z};
                m.normals[i] = {c * n.x + s * n.z, n.y, -s * n.x + c * n.z};
            }
            room.fence.append(m);
        }
        room.fenceColorLinear = lin(skin.fence);
    }
    return room;
}

namespace {

void gpuQuad(std::vector<render::RoomVertex>& out, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
             const glm::vec3& d, const glm::vec3& n, const glm::vec3& albedo, float roughness, float material) {
    const glm::vec4 ar(albedo, roughness);
    for (const glm::vec3& p : {a, b, c, a, c, d}) out.push_back({p, n, ar, material});
}

void gpuWall(std::vector<render::RoomVertex>& out, const Skin& skin, const glm::vec3& origin, const glm::vec3& along,
             const glm::vec3& normal, float width, float height) {
    const glm::vec3 up{0.0f, 1.0f, 0.0f};
    constexpr int kRows = 12;
    for (int j = 0; j < kRows; ++j) {
        const float y0 = height * static_cast<float>(j) / kRows;
        const float y1 = height * static_cast<float>(j + 1) / kRows;
        const float t = (y0 + y1) * 0.5f / height;
        const glm::vec3 c = y1 <= 0.12f ? skin.baseboard : glm::mix(skin.wallBottom, skin.wallTop, t);
        gpuQuad(out, origin + up * y0, origin + along * width + up * y0, origin + along * width + up * y1,
                origin + up * y1, normal, lin(c), y1 <= 0.12f ? 0.45f : 0.85f, 0.0f);
    }
    const glm::vec3 inward = normal * 0.002f;
    for (int i = 1; i < static_cast<int>(width / 0.42f); ++i) {
        for (int j = 1; j < 5; ++j) {
            const std::uint32_t h = hash(i, j + static_cast<int>(origin.x * 10.0f + origin.z * 7.0f));
            const float cx = static_cast<float>(i) * 0.42f + ((j % 2) ? 0.21f : 0.0f);
            if (cx > width - 0.1f) continue;
            const float cy = 0.35f + static_cast<float>(j) * 0.36f;
            const float r = 0.045f + 0.02f * static_cast<float>(h % 3);
            const glm::vec3 center = origin + along * cx + up * cy + inward;
            const glm::vec4 ar(lin(skin.dots[h % skin.dots.size()]), 0.8f);
            constexpr int kSeg = 24;
            for (int k = 0; k < kSeg; ++k) {
                const float a0 = glm::two_pi<float>() * static_cast<float>(k) / kSeg;
                const float a1 = glm::two_pi<float>() * static_cast<float>(k + 1) / kSeg;
                out.push_back({center, normal, ar, 0.0f});
                out.push_back({center + (along * std::cos(a0) + up * std::sin(a0)) * r, normal, ar, 0.0f});
                out.push_back({center + (along * std::cos(a1) + up * std::sin(a1)) * r, normal, ar, 0.0f});
            }
        }
    }
}

} // namespace

std::vector<render::RoomVertex> buildGpuRoom(const Skin& skin, const simulation::PlayArea& area) {
    std::vector<render::RoomVertex> out;
    const float R = 2.4f;
    constexpr float kWallHeight = 2.4f;
    gpuWall(out, skin, {-R, -0.03f, -R}, {1, 0, 0}, {0, 0, 1}, 2.0f * R, kWallHeight);
    gpuWall(out, skin, {R, -0.03f, R}, {-1, 0, 0}, {0, 0, -1}, 2.0f * R, kWallHeight);
    gpuWall(out, skin, {R, -0.03f, -R}, {0, 0, 1}, {-1, 0, 0}, 2.0f * R, kWallHeight);
    gpuWall(out, skin, {-R, -0.03f, R}, {0, 0, -1}, {1, 0, 0}, 2.0f * R, kWallHeight);

    // EVA mat: tiles (top at y = 0) plus the cut edge all around.
    const float h = area.halfSize;
    const int tiles = std::max(1, static_cast<int>(std::round(2.0f * h / 0.3f)));
    const float size = 2.0f * h / static_cast<float>(tiles);
    const glm::vec3 up{0.0f, 1.0f, 0.0f};
    for (int ix = 0; ix < tiles; ++ix) {
        for (int iz = 0; iz < tiles; ++iz) {
            const glm::vec3 c = lin(skin.matTiles[static_cast<std::size_t>((ix + 2 * iz) % 4)]);
            // Shared edges computed identically on both sides: no cracks.
            const float x0 = -h + static_cast<float>(ix) * size, x1 = -h + static_cast<float>(ix + 1) * size;
            const float z0 = -h + static_cast<float>(iz) * size, z1 = -h + static_cast<float>(iz + 1) * size;
            gpuQuad(out, {x0, 0.0f, z0}, {x0, 0.0f, z1}, {x1, 0.0f, z1}, {x1, 0.0f, z0}, up, c, 0.92f, 1.0f);
        }
    }
    const float b = -kMatThickness;
    const glm::vec3 edge = lin(skin.matTiles[0]) * 0.9f;
    gpuQuad(out, {-h, b, h}, {h, b, h}, {h, 0, h}, {-h, 0, h}, {0, 0, 1}, edge, 0.92f, 1.0f);
    gpuQuad(out, {h, b, -h}, {-h, b, -h}, {-h, 0, -h}, {h, 0, -h}, {0, 0, -1}, edge, 0.92f, 1.0f);
    gpuQuad(out, {h, b, h}, {h, b, -h}, {h, 0, -h}, {h, 0, h}, {1, 0, 0}, edge, 0.92f, 1.0f);
    gpuQuad(out, {-h, b, -h}, {-h, b, h}, {-h, 0, h}, {-h, 0, -h}, {-1, 0, 0}, edge, 0.92f, 1.0f);
    return out;
}

} // namespace woodyn::viewer
