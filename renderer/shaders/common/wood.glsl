// Port of engine/src/wood/WoodSource.cpp + WoodCut.cpp + materials/Stain.cpp
// (D-35: surfaceAlbedo is the single source of truth; this must match it).
#ifndef WOODYN_WOOD_GLSL
#define WOODYN_WOOD_GLSL

#include "noise.glsl"

struct WoodParams {
    vec4 pithRing;    // pith.x, pith.y, ringWidth, ringWobble
    vec4 contrastRay; // ringContrast, rayDensity, rayDarkening, poreBand
    vec4 fiberSeed;   // fiberNoise, seed (uint bits), length, radius
    vec4 earlywood;   // linear rgb
    vec4 latewood;    // linear rgb
};

struct Grain {
    vec3 color;
    float phase;
    float late;
    float ray;
};

vec3 wd_rotate(vec4 q, vec3 v) {
    // q = (x, y, z, w)
    vec3 t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

Grain wd_sampleGrain(vec3 p, WoodParams s) {
    uint seed = floatBitsToUint(s.fiberSeed.y);
    vec2 xy = p.xy - s.pithRing.xy;

    float wobble = s.pithRing.w * wd_fbm(vec3(xy * 18.0, p.z * 4.0), seed, 3);
    float r = length(xy) + wobble;
    float phase = r / s.pithRing.z;
    float f = phase - floor(phase);

    float late = smoothstep(0.55, 0.92, f) * (1.0 - smoothstep(0.97, 1.0, f));
    late *= 0.75 + 0.5 * wd_hash01(int(floor(phase)), 0, 0, seed);

    vec3 color = mix(s.earlywood.rgb, s.latewood.rgb, clamp(late * s.contrastRay.x / 0.35, 0.0, 1.0));

    if (s.contrastRay.w > 0.0 && f < 0.18) {
        float pores = wd_gradientNoise(vec3(xy * 2600.0, p.z * 90.0), seed + 7u);
        if (pores > 0.25) color *= 1.0 - 0.35 * s.contrastRay.w;
    }

    float ray = 0.0;
    if (s.contrastRay.y > 0.0 && r > 0.002) {
        float angle = atan(xy.y, xy.x);
        float across = angle * r / 0.00045;
        int cell = int(floor(across));
        float local = across - floor(across);
        int zCell = int(floor(p.z / 0.004 + wd_hash01(cell, 9, 9, seed)));
        int rCell = int(floor(r / 0.01));
        if (wd_hash01(cell, zCell, rCell, seed + 3u) < s.contrastRay.y && abs(local - 0.5) < 0.2) {
            color *= 1.0 - s.contrastRay.z;
            color.r *= 1.04;
            ray = 1.0;
        }
    }

    float streak = 0.7 * wd_gradientNoise(vec3(xy * 330.0, p.z * 12.0), seed + 11u) +
                   0.3 * wd_gradientNoise(vec3(xy * 1250.0, p.z * 40.0), seed + 13u);
    color *= 1.0 + s.fiberSeed.x * streak;
    color *= 1.0 + 0.06 * wd_fbm(p * 6.0, seed + 17u, 2);

    Grain g;
    g.color = clamp(color, vec3(0.0), vec3(1.0));
    g.phase = phase;
    g.late = late;
    g.ray = ray;
    return g;
}

/// Cheap micro-height of the surface (latewood is harder and stands slightly
/// proud after sanding; fibers leave faint ridges). Unitless.
float wd_height(vec3 p, WoodParams s) {
    uint seed = floatBitsToUint(s.fiberSeed.y);
    vec2 xy = p.xy - s.pithRing.xy;
    float r = length(xy) + s.pithRing.w * wd_fbm(vec3(xy * 18.0, p.z * 4.0), seed, 3);
    float phase = r / s.pithRing.z;
    float f = phase - floor(phase);
    float late = smoothstep(0.55, 0.92, f) * (1.0 - smoothstep(0.97, 1.0, f));
    float streak = wd_gradientNoise(vec3(xy * 330.0, p.z * 12.0), seed + 11u);
    return late + 0.35 * streak;
}

float wd_endGrainFactor(vec4 cutRot, vec3 localNormal) {
    float alignment = abs(wd_rotate(cutRot, normalize(localNormal)).z);
    float t = clamp((alignment - 0.55) / (0.85 - 0.55), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

vec3 wd_applyStain(vec3 wood, vec3 dye, bool stained) {
    if (!stained) return wood;
    const float coverage = 0.88;
    const vec3 luma = vec3(0.2126, 0.7152, 0.0722);
    vec3 naturalRef = pow(vec3(0.87, 0.71, 0.53), vec3(2.2));
    float variation = dot(wood, luma) / dot(naturalRef, luma);
    return mix(wood, dye * variation, coverage);
}

vec3 wd_surfaceAlbedo(Grain g, float endGrain, vec3 dye, bool stained) {
    vec3 wood = g.color * (1.0 - 0.16 * endGrain);
    if (!stained) return wood;
    vec3 s = wd_applyStain(wood, dye, true);
    return mix(s, s * 0.85, endGrain);
}

#endif
