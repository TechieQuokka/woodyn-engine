// Physically based shading: GGX microfacets, height-correlated Smith, Schlick
// Fresnel, Lambert diffuse, split-sum IBL with an analytic environment BRDF,
// optional clear coat (lacquer / varnish) layer.
#ifndef WOODYN_PBR_GLSL
#define WOODYN_PBR_GLSL

const float PI = 3.14159265359;

float pbr_D_GGX(float NoH, float a) {
    float a2 = a * a;
    float d = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float pbr_V_SmithGGXCorrelated(float NoV, float NoL, float a) {
    float a2 = a * a;
    float gv = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    float gl = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    return 0.5 / max(gv + gl, 1e-5);
}

vec3 pbr_F_Schlick(vec3 f0, float VoH) {
    return f0 + (vec3(1.0) - f0) * pow(1.0 - VoH, 5.0);
}

float pbr_F_Schlick1(float f0, float VoH) {
    return f0 + (1.0 - f0) * pow(1.0 - VoH, 5.0);
}

// Karis' analytic approximation of the split-sum environment BRDF.
vec2 pbr_envBRDF(float NoV, float roughness) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    return vec2(-1.04, 1.04) * a004 + r.zw;
}

// 9-coefficient spherical harmonics irradiance (coefficients pre-convolved).
vec3 pbr_shIrradiance(vec4 sh[9], vec3 n) {
    return max(vec3(0.0),
        sh[0].rgb +
        sh[1].rgb * n.y + sh[2].rgb * n.z + sh[3].rgb * n.x +
        sh[4].rgb * (n.x * n.y) + sh[5].rgb * (n.y * n.z) +
        sh[6].rgb * (3.0 * n.z * n.z - 1.0) +
        sh[7].rgb * (n.x * n.z) + sh[8].rgb * (n.x * n.x - n.y * n.y));
}

#endif
