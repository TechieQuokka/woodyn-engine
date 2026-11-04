#version 460
// 5x5 depth-aware blur of the AO/contact buffer.
layout(location = 0) in vec2 vUV;
layout(set = 2, binding = 0) uniform sampler2D aoMap;
layout(set = 2, binding = 1) uniform sampler2D depthMap;
layout(std140, set = 3, binding = 0) uniform Params { vec4 texel; vec4 depthParams; } u; // depthParams.x near, .y far
layout(location = 0) out vec4 outAO;
float linearDepth(float d) { return u.depthParams.x * u.depthParams.y / (u.depthParams.y - d * (u.depthParams.y - u.depthParams.x)); }
void main() {
    float center = linearDepth(texture(depthMap, vUV).r);
    vec2 sum = vec2(0.0);
    float wsum = 0.0;
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            vec2 uv = vUV + vec2(x, y) * u.texel.xy;
            float d = linearDepth(texture(depthMap, uv).r);
            float w = exp(-abs(d - center) / max(center * 0.02, 1e-4));
            sum += texture(aoMap, uv).rg * w;
            wsum += w;
        }
    }
    outAO = vec4(sum / max(wsum, 1e-5), 0.0, 1.0);
}
