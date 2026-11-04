#ifndef WOODYN_LIGHTING_GLSL
#define WOODYN_LIGHTING_GLSL
#include "pbr.glsl"

layout(set = 2, binding = 0) uniform sampler2DShadow shadowMap;
layout(set = 2, binding = 1) uniform samplerCube envMap;
layout(set = 2, binding = 2) uniform sampler2D aoMap;

float sunShadow(vec3 worldPos, vec3 n) {
    // Normal offset (in texels) against acne on curved/rounded surfaces.
    vec3 p = worldPos + n * (frame.params.w * 1.5);
    vec4 c = frame.lightViewProj * vec4(p, 1.0);
    vec3 ndc = c.xyz / c.w;
    vec2 uv = vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) || ndc.z > 1.0) return 1.0;
    // 16-tap Poisson PCF, ~2.5 texel radius: soft but contact-tight shadows.
    const vec2 poisson[16] = vec2[16](
        vec2(-0.94201624, -0.39906216), vec2(0.94558609, -0.76890725), vec2(-0.09418410, -0.92938870),
        vec2(0.34495938, 0.29387760), vec2(-0.91588581, 0.45771432), vec2(-0.81544232, -0.87912464),
        vec2(-0.38277543, 0.27676845), vec2(0.97484398, 0.75648379), vec2(0.44323325, -0.97511554),
        vec2(0.53742981, -0.47373420), vec2(-0.26496911, -0.41893023), vec2(0.79197514, 0.19090188),
        vec2(-0.24188840, 0.99706507), vec2(-0.81409955, 0.91437590), vec2(0.19984126, 0.78641367),
        vec2(0.14383161, -0.14100790));
    float texel = 1.0 / float(textureSize(shadowMap, 0).x);
    float sum = 0.0;
    for (int i = 0; i < 16; ++i) {
        sum += texture(shadowMap, vec3(uv + poisson[i] * texel * 2.5, ndc.z));
    }
    return sum / 16.0;
}

vec2 screenAO() {
    vec2 uv = gl_FragCoord.xy * frame.viewport.zw;
    vec2 ac = texture(aoMap, uv).rg;
    float ao = frame.params.x > 0.5 ? ac.r : 1.0;
    float contact = frame.params.y > 0.5 ? ac.g : 1.0;
    return vec2(ao, contact);
}

struct Surface {
    vec3 albedo;
    float roughness;     // base layer, perceptual
    vec3 N;              // shading normal (with micro detail)
    vec3 Ngeo;           // smooth geometric normal (clear coat sits on top)
    float coat;          // clear coat strength 0..1
    float coatRoughness;
};

vec3 shade(Surface s, vec3 worldPos) {
    vec3 V = normalize(frame.eye.xyz - worldPos);
    vec3 L = normalize(frame.sunDir.xyz);
    float NoV = clamp(dot(s.N, V), 1e-4, 1.0);
    float NoL = clamp(dot(s.N, L), 0.0, 1.0);
    vec3 H = normalize(V + L);
    float NoH = clamp(dot(s.N, H), 0.0, 1.0);
    float VoH = clamp(dot(V, H), 0.0, 1.0);

    vec2 occ = screenAO();
    float shadow = sunShadow(worldPos, s.Ngeo) * occ.y;

    // Base layer: dielectric wood/paint, F0 = 0.04.
    float a = max(s.roughness * s.roughness, 0.002);
    vec3 F0 = vec3(0.04);
    vec3 F = pbr_F_Schlick(F0, VoH);
    vec3 spec = pbr_D_GGX(NoH, a) * pbr_V_SmithGGXCorrelated(NoV, NoL, a) * F;
    vec3 diff = (vec3(1.0) - F) * s.albedo / PI;
    vec3 direct = (diff + spec) * frame.sunColor.rgb * NoL * shadow;

    vec2 ab = pbr_envBRDF(NoV, s.roughness);
    vec3 R = reflect(-V, s.N);
    float maxMip = frame.params.z;
    vec3 envSpec = textureLod(envMap, R, s.roughness * maxMip).rgb * (F0 * ab.x + ab.y);
    vec3 envDiff = s.albedo * pbr_shIrradiance(frame.sh, s.N) * (1.0 - (F0 * ab.x + ab.y));
    vec3 ambient = (envDiff + envSpec) * occ.x;

    vec3 base = direct + ambient;
    if (s.coat <= 0.0) return base;

    // Clear coat (lacquer): smooth dielectric layer on the geometric normal.
    float cNoV = clamp(dot(s.Ngeo, V), 1e-4, 1.0);
    float cNoL = clamp(dot(s.Ngeo, L), 0.0, 1.0);
    float cNoH = clamp(dot(s.Ngeo, H), 0.0, 1.0);
    float ca = max(s.coatRoughness * s.coatRoughness, 0.002);
    float Fc = pbr_F_Schlick1(0.04, VoH) * s.coat;
    float coatDirect = pbr_D_GGX(cNoH, ca) * pbr_V_SmithGGXCorrelated(cNoV, cNoL, ca) * Fc * cNoL * shadow;
    vec2 cab = pbr_envBRDF(cNoV, s.coatRoughness);
    vec3 cR = reflect(-V, s.Ngeo);
    vec3 coatEnv = textureLod(envMap, cR, s.coatRoughness * maxMip).rgb * (0.04 * cab.x + cab.y) * s.coat * occ.x;
    float FcV = pbr_F_Schlick1(0.04, cNoV) * s.coat;
    return base * (1.0 - FcV) + coatDirect * frame.sunColor.rgb + coatEnv;
}
#endif
