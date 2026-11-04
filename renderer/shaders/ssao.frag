#version 460
// Screen-space ambient occlusion + contact shadows (RG output).
layout(location = 0) in vec2 vUV;
layout(set = 2, binding = 0) uniform sampler2D depthMap;
layout(set = 2, binding = 1) uniform sampler2D normalMap;
layout(std140, set = 3, binding = 0) uniform Params {
    mat4 invViewProj;
    mat4 viewProj;
    vec4 eye;
    vec4 sunDir;
    vec4 settings; // x: AO radius [m], y: contact length [m], z: AO strength, w: contact thickness [m]
} u;
layout(location = 0) out vec4 outAO;

vec3 worldAt(vec2 uv) {
    float d = texture(depthMap, uv).r;
    vec4 w = u.invViewProj * vec4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, d, 1.0);
    return w.xyz / w.w;
}

bool project(vec3 p, out vec2 uv) {
    vec4 c = u.viewProj * vec4(p, 1.0);
    if (c.w <= 0.0) return false;
    vec2 ndc = c.xy / c.w;
    uv = vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    return all(greaterThanEqual(uv, vec2(0.0))) && all(lessThanEqual(uv, vec2(1.0)));
}

float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }

void main() {
    float depth = texture(depthMap, vUV).r;
    if (depth >= 1.0) { outAO = vec4(1.0); return; }
    vec3 P = worldAt(vUV);
    vec3 N = normalize(texture(normalMap, vUV).xyz);
    float distP = length(P - u.eye.xyz);

    // Hemisphere AO: 16 samples, rotated per pixel.
    vec3 t = normalize(abs(N.y) < 0.9 ? cross(N, vec3(0, 1, 0)) : cross(N, vec3(1, 0, 0)));
    vec3 b = cross(N, t);
    float rot = hash(gl_FragCoord.xy) * 6.2831853;
    float occlusion = 0.0;
    const int kSamples = 16;
    for (int i = 0; i < kSamples; ++i) {
        float fi = float(i);
        float r = sqrt((fi + 0.5) / float(kSamples));
        float phi = fi * 2.39996323 + rot;
        float z = sqrt(max(0.0, 1.0 - r * r));
        vec3 dir = t * (r * cos(phi)) + b * (r * sin(phi)) + N * z;
        float scale = mix(0.15, 1.0, fract(fi * 0.618 + rot));
        vec3 S = P + N * 0.0008 + dir * (u.settings.x * scale);
        vec2 suv;
        if (!project(S, suv)) continue;
        vec3 Q = worldAt(suv);
        float sceneDist = length(Q - u.eye.xyz);
        float sampleDist = length(S - u.eye.xyz);
        float range = smoothstep(0.0, 1.0, u.settings.x / max(length(Q - P), 1e-4));
        occlusion += (sceneDist < sampleDist - 0.0004 ? 1.0 : 0.0) * range;
    }
    float ao = clamp(1.0 - u.settings.z * occlusion / float(kSamples), 0.0, 1.0);

    // Contact shadow: short ray march toward the sun in screen space.
    vec3 L = normalize(u.sunDir.xyz);
    float contact = 1.0;
    const int kSteps = 14;
    float jitter = hash(gl_FragCoord.yx + 3.1);
    for (int i = 1; i <= kSteps; ++i) {
        vec3 S = P + N * 0.0006 + L * (u.settings.y * (float(i) - jitter) / float(kSteps));
        vec2 suv;
        if (!project(S, suv)) break;
        vec3 Q = worldAt(suv);
        float diff = length(S - u.eye.xyz) - length(Q - u.eye.xyz);
        if (diff > 0.0003 && diff < u.settings.w) { contact = 0.0; break; }
    }
    outAO = vec4(ao, mix(1.0, contact, smoothstep(0.0, 0.2, dot(N, L))), 0.0, 1.0);
}
