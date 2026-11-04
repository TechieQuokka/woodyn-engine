#include "woodyn/wood/Noise.hpp"

#include <glm/geometric.hpp>

#include <cmath>

namespace woodyn::wood {
namespace {

std::uint32_t mix(std::uint32_t h) {
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

std::uint32_t hash3(std::int32_t x, std::int32_t y, std::int32_t z, std::uint32_t seed) {
    std::uint32_t h = seed * 0x9e3779b9u;
    h = mix(h ^ static_cast<std::uint32_t>(x) * 0x85ebca6bu);
    h = mix(h ^ static_cast<std::uint32_t>(y) * 0xc2b2ae35u);
    h = mix(h ^ static_cast<std::uint32_t>(z) * 0x27d4eb2fu);
    return h;
}

glm::vec3 gradient(std::int32_t x, std::int32_t y, std::int32_t z, std::uint32_t seed) {
    // 12 edge directions of a cube (classic improved-Perlin set).
    static constexpr float g[12][3] = {{1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0},
                                       {1, 0, 1}, {-1, 0, 1}, {1, 0, -1}, {-1, 0, -1},
                                       {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1}};
    const auto i = hash3(x, y, z, seed) % 12u;
    return {g[i][0], g[i][1], g[i][2]};
}

float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

} // namespace

float hash01(std::int32_t x, std::int32_t y, std::int32_t z, std::uint32_t seed) {
    return static_cast<float>(hash3(x, y, z, seed) >> 8) * (1.0f / 16777216.0f);
}

float gradientNoise(const glm::vec3& p, std::uint32_t seed) {
    const glm::vec3 f = glm::floor(p);
    const auto x0 = static_cast<std::int32_t>(f.x);
    const auto y0 = static_cast<std::int32_t>(f.y);
    const auto z0 = static_cast<std::int32_t>(f.z);
    const glm::vec3 t = p - f;
    float n[8];
    for (int c = 0; c < 8; ++c) {
        const int dx = c & 1, dy = (c >> 1) & 1, dz = (c >> 2) & 1;
        const glm::vec3 d = t - glm::vec3(static_cast<float>(dx), static_cast<float>(dy), static_cast<float>(dz));
        n[c] = glm::dot(gradient(x0 + dx, y0 + dy, z0 + dz, seed), d);
    }
    const float u = fade(t.x), v = fade(t.y), w = fade(t.z);
    const float x00 = n[0] + u * (n[1] - n[0]);
    const float x10 = n[2] + u * (n[3] - n[2]);
    const float x01 = n[4] + u * (n[5] - n[4]);
    const float x11 = n[6] + u * (n[7] - n[6]);
    const float y0v = x00 + v * (x10 - x00);
    const float y1v = x01 + v * (x11 - x01);
    return y0v + w * (y1v - y0v);
}

float fbm(const glm::vec3& p, std::uint32_t seed, int octaves) {
    float sum = 0.0f;
    float amp = 0.5f;
    glm::vec3 q = p;
    for (int i = 0; i < octaves; ++i) {
        sum += amp * gradientNoise(q, seed + static_cast<std::uint32_t>(i) * 101u);
        q *= 2.0f;
        amp *= 0.5f;
    }
    return sum;
}

} // namespace woodyn::wood
