// Port of engine/src/wood/Noise.cpp. Must stay bit-compatible in its integer
// hashing so GPU grain matches the CPU reference (verified by GrainProbe tests).
#ifndef WOODYN_NOISE_GLSL
#define WOODYN_NOISE_GLSL

uint wd_mix(uint h) {
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

uint wd_hash3(int x, int y, int z, uint seed) {
    uint h = seed * 0x9e3779b9u;
    h = wd_mix(h ^ (uint(x) * 0x85ebca6bu));
    h = wd_mix(h ^ (uint(y) * 0xc2b2ae35u));
    h = wd_mix(h ^ (uint(z) * 0x27d4eb2fu));
    return h;
}

float wd_hash01(int x, int y, int z, uint seed) {
    return float(wd_hash3(x, y, z, seed) >> 8) * (1.0 / 16777216.0);
}

vec3 wd_gradient(int x, int y, int z, uint seed) {
    const vec3 g[12] = vec3[12](vec3(1, 1, 0), vec3(-1, 1, 0), vec3(1, -1, 0), vec3(-1, -1, 0),
                                vec3(1, 0, 1), vec3(-1, 0, 1), vec3(1, 0, -1), vec3(-1, 0, -1),
                                vec3(0, 1, 1), vec3(0, -1, 1), vec3(0, 1, -1), vec3(0, -1, -1));
    return g[wd_hash3(x, y, z, seed) % 12u];
}

float wd_fade(float t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }

float wd_gradientNoise(vec3 p, uint seed) {
    vec3 f = floor(p);
    ivec3 i0 = ivec3(f);
    vec3 t = p - f;
    float n[8];
    for (int c = 0; c < 8; ++c) {
        int dx = c & 1, dy = (c >> 1) & 1, dz = (c >> 2) & 1;
        vec3 d = t - vec3(dx, dy, dz);
        n[c] = dot(wd_gradient(i0.x + dx, i0.y + dy, i0.z + dz, seed), d);
    }
    float u = wd_fade(t.x), v = wd_fade(t.y), w = wd_fade(t.z);
    float x00 = n[0] + u * (n[1] - n[0]);
    float x10 = n[2] + u * (n[3] - n[2]);
    float x01 = n[4] + u * (n[5] - n[4]);
    float x11 = n[6] + u * (n[7] - n[6]);
    float y0 = x00 + v * (x10 - x00);
    float y1 = x01 + v * (x11 - x01);
    return y0 + w * (y1 - y0);
}

float wd_fbm(vec3 p, uint seed, int octaves) {
    float sum = 0.0;
    float amp = 0.5;
    vec3 q = p;
    for (int i = 0; i < octaves; ++i) {
        sum += amp * wd_gradientNoise(q, seed + uint(i) * 101u);
        q *= 2.0;
        amp *= 0.5;
    }
    return sum;
}

#endif
