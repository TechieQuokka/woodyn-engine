#version 460
// Exposure, bloom, Khronos PBR Neutral tone mapping (keeps the toy colors
// true), subtle vignette, sRGB encoding.
layout(location = 0) in vec2 vUV;
layout(set = 2, binding = 0) uniform sampler2D hdrMap;
layout(set = 2, binding = 1) uniform sampler2D bloomMap;
layout(std140, set = 3, binding = 0) uniform Params { vec4 p; } u; // x exposure, y bloom strength, z vignette
layout(location = 0) out vec4 outColor;

vec3 pbrNeutral(vec3 color) {
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;
    float x = min(color.r, min(color.g, color.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;
    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression) return color;
    const float d = 1.0 - startCompression;
    float newPeak = 1.0 - d * d / (peak + d - startCompression);
    color *= newPeak / peak;
    float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return mix(color, vec3(newPeak), g);
}

vec3 srgb(vec3 c) {
    c = clamp(c, 0.0, 1.0);
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}

void main() {
    vec3 hdr = texture(hdrMap, vUV).rgb;
    vec3 bloom = texture(bloomMap, vUV).rgb;
    vec3 c = (hdr + bloom * u.p.y) * u.p.x;
    c = pbrNeutral(c);
    vec2 q = vUV - 0.5;
    c *= 1.0 - u.p.z * dot(q, q) * 1.6;
    outColor = vec4(srgb(c), 1.0);
}
