#version 460
// Depth of field (thin lens, scatter-as-gather). Strong settings give the
// "tilt-shift miniature" look of macro photos of toys.
layout(location = 0) in vec2 vUV;
layout(set = 2, binding = 0) uniform sampler2D colorMap;
layout(set = 2, binding = 1) uniform sampler2D depthMap;
layout(std140, set = 3, binding = 0) uniform Params {
    mat4 invViewProj;
    vec4 eye;
    vec4 lens;     // x: focus distance [m], y: CoC scale [px], z: max CoC [px], w: enabled
    vec4 viewport; // w, h, 1/w, 1/h
} u;
layout(location = 0) out vec4 outColor;

float distanceAt(vec2 uv) {
    float d = texture(depthMap, uv).r;
    vec4 w = u.invViewProj * vec4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, d, 1.0);
    return length(w.xyz / w.w - u.eye.xyz);
}
float coc(float z) { return min(u.lens.z, u.lens.y * abs(z - u.lens.x) / max(z, 1e-4)); }

void main() {
    vec3 center = texture(colorMap, vUV).rgb;
    if (u.lens.w < 0.5) { outColor = vec4(center, 1.0); return; }
    float zc = distanceAt(vUV);
    float cc = coc(zc);
    vec3 sum = center;
    float wsum = 1.0;
    const int kTaps = 48;
    float radius = u.lens.z;
    for (int i = 0; i < kTaps; ++i) {
        float fi = float(i) + 0.5;
        float r = sqrt(fi / float(kTaps)) * radius;
        float a = fi * 2.39996323;
        vec2 offset = vec2(cos(a), sin(a)) * r;
        vec2 uv = vUV + offset * u.viewport.zw;
        float zs = distanceAt(uv);
        float cs = coc(zs);
        // Background may not bleed over a sharper foreground.
        if (zs > zc) cs = min(cs, cc);
        float w = clamp(cs - r + 1.0, 0.0, 1.0);
        sum += texture(colorMap, uv).rgb * w;
        wsum += w;
    }
    outColor = vec4(sum / wsum, 1.0);
}
