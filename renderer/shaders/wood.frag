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

layout(std430, set = 2, binding = 3) readonly buffer Instances { Instance instances[]; };
layout(std430, set = 2, binding = 4) readonly buffer Woods { WoodParams woods[]; };

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

    mat3 m = mat3(inst.model);
    Surface s;
    s.albedo = albedo;
    s.Ngeo = normalize(vWorldNormal);
    s.N = normalize(m * nBumped);
    bool floorVarnish = inst.misc.y > 0.5;
    // Lacquered toy: satin base, latewood and rays a bit smoother, end grain rougher.
    s.roughness = clamp((floorVarnish ? 0.38 : 0.46) - 0.08 * g.late - 0.06 * g.ray + 0.20 * endGrain, 0.08, 1.0);
    s.coat = floorVarnish ? 0.9 : 0.55;
    s.coatRoughness = floorVarnish ? 0.12 : 0.22;

    vec3 color = shade(s, vWorldPos);
    float highlight = inst.misc.x;
    color = color * highlight + albedo * 0.06 * max(highlight - 1.0, 0.0) * 10.0;
    outColor = vec4(color, 1.0);
}
