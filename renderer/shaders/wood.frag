#version 460
#include "instance.glsl"
#include "frame.glsl"
#include "wood.glsl"
#include "lighting.glsl"

layout(location = 0) in vec3 vLocalPos;
layout(location = 1) in vec3 vLocalNormal;
layout(location = 2) in vec3 vWorldPos;
layout(location = 3) in vec3 vWorldNormal;
layout(location = 4) flat in uint vInstance;

layout(set = 2, binding = 3) uniform sampler2D fiberDetail; // CC0 photo micro detail (D-39 stage 2)
layout(std430, set = 2, binding = 4) readonly buffer Instances { Instance instances[]; };
layout(std430, set = 2, binding = 5) readonly buffer Woods { WoodParams woods[]; };

// Photo micro detail projected along the fibers in log space: two planes that
// contain the fiber axis (+Z), blended by the surface normal. End grain gets
// none (the photo shows side grain only). Each log uses its own offset.
struct Detail {
    float albedo;   // luminance ratio around 1
    vec3 normalLog; // perturbation in log space
    float rough;    // roughness ratio around 1
};

Detail photoDetail(vec3 logPos, vec3 nLog, uint seed) {
    vec2 offset = vec2(wd_hash01(1, 2, 3, seed), wd_hash01(4, 5, 6, seed)) * 7.0;
    float repeat = frame.detail.y;
    float wx = nLog.x * nLog.x;
    float wy = nLog.y * nLog.y;
    vec4 a = texture(fiberDetail, vec2(logPos.y, logPos.z) / repeat + offset);       // plane YZ (seen along X)
    vec4 b = texture(fiberDetail, vec2(logPos.x, logPos.z) / repeat + offset.yx);    // plane XZ (seen along Y)
    float wsum = max(wx + wy, 1e-5);
    vec4 d = (a * wx + b * wy) / wsum;
    vec2 na = a.gb * 2.0 - 1.0;
    vec2 nb = b.gb * 2.0 - 1.0;
    // Texture u runs across the fibers, v along them (+Z).
    vec3 pa = vec3(0.0, na.x, na.y);
    vec3 pb = vec3(nb.x, 0.0, nb.y);
    Detail r;
    r.albedo = 1.0 + (d.r - 0.5) * 2.0 * 0.5;
    r.normalLog = (pa * wx + pb * wy) / wsum;
    r.rough = 1.0 + (d.a - 0.5) * 0.8;
    return r;
}

layout(location = 0) out vec4 outColor;

void main() {
    Instance inst = instances[vInstance];
    WoodParams wp = woods[int(inst.cutCenter.w)];
    vec3 nLocal = normalize(vLocalNormal);
    vec3 logPos = inst.cutCenter.xyz + wd_rotate(inst.cutRot, vLocalPos);

    Grain g = wd_sampleGrain(logPos, wp);
    float endGrain = wd_endGrainFactor(inst.cutRot, nLocal);
    bool stained = inst.stain.w > 0.5;
    vec3 albedo = wd_surfaceAlbedo(g, endGrain, inst.stain.rgb, stained);

    // Micro relief from the grain (finite differences in log space).
    vec3 t1 = normalize(abs(nLocal.y) < 0.9 ? cross(nLocal, vec3(0, 1, 0)) : cross(nLocal, vec3(1, 0, 0)));
    vec3 t2 = cross(nLocal, t1);
    const float eps = 0.00015;
    float h0 = wd_height(logPos, wp);
    float h1 = wd_height(logPos + wd_rotate(inst.cutRot, t1 * eps), wp);
    float h2 = wd_height(logPos + wd_rotate(inst.cutRot, t2 * eps), wp);
    const float amplitude = 0.000035; // m of relief per unit height
    vec3 gradLocal = ((h1 - h0) * t1 + (h2 - h0) * t2) * (amplitude / eps);
    vec3 nBumped = normalize(nLocal - gradLocal * (1.0 - 0.6 * endGrain));

    // Photo micro detail on side grain (pores, fine fibers).
    float detailStrength = frame.detail.x * (1.0 - endGrain);
    float roughDetail = 1.0;
    if (detailStrength > 0.0) {
        vec3 nLog = wd_rotate(inst.cutRot, nLocal);
        Detail d = photoDetail(logPos, nLog, floatBitsToUint(wp.fiberSeed.y));
        albedo *= mix(1.0, d.albedo, detailStrength);
        vec4 inv = vec4(-inst.cutRot.xyz, inst.cutRot.w);
        vec3 pLocal = wd_rotate(inv, d.normalLog);
        nBumped = normalize(nBumped + (pLocal - nBumped * dot(pLocal, nBumped)) * (0.35 * detailStrength));
        roughDetail = mix(1.0, d.rough, detailStrength);
    }

    mat3 m = mat3(inst.model);
    Surface s;
    s.albedo = albedo;
    s.Ngeo = normalize(vWorldNormal);
    s.N = normalize(m * nBumped);
    bool floorVarnish = inst.misc.y > 0.5;
    // Lacquered toy: satin base, latewood and rays a bit smoother, end grain rougher.
    s.roughness = clamp(((floorVarnish ? 0.38 : 0.46) - 0.08 * g.late - 0.06 * g.ray + 0.20 * endGrain) * roughDetail, 0.08, 1.0);
    s.coat = floorVarnish ? 0.9 : 0.55;
    s.coatRoughness = floorVarnish ? 0.12 : 0.22;

    vec3 color = shade(s, vWorldPos);
    float highlight = inst.misc.x;
    color = color * highlight + albedo * 0.06 * max(highlight - 1.0, 0.0) * 10.0;
    outColor = vec4(color, 1.0);
}
