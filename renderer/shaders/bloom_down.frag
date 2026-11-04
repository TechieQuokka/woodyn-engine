#version 460
// 13-tap downsample (Jimenez 2014); the first pass applies a soft threshold
// and Karis averaging to suppress fireflies.
layout(location = 0) in vec2 vUV;
layout(set = 2, binding = 0) uniform sampler2D src;
layout(std140, set = 3, binding = 0) uniform Params { vec4 texel; vec4 threshold; } u; // threshold.x value, .y knee, .z first pass
layout(location = 0) out vec4 outColor;
vec3 tap(vec2 o) { return texture(src, vUV + o * u.texel.xy).rgb; }
float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
vec3 karis(vec3 a, vec3 b, vec3 c, vec3 d) {
    float wa = 1.0 / (1.0 + luma(a)), wb = 1.0 / (1.0 + luma(b)), wc = 1.0 / (1.0 + luma(c)), wd = 1.0 / (1.0 + luma(d));
    return (a * wa + b * wb + c * wc + d * wd) / (wa + wb + wc + wd);
}
void main() {
    vec3 a = tap(vec2(-2, -2)), b = tap(vec2(0, -2)), c = tap(vec2(2, -2));
    vec3 d = tap(vec2(-1, -1)), e = tap(vec2(1, -1));
    vec3 f = tap(vec2(-2, 0)), g = tap(vec2(0, 0)), h = tap(vec2(2, 0));
    vec3 i = tap(vec2(-1, 1)), j = tap(vec2(1, 1));
    vec3 k = tap(vec2(-2, 2)), l = tap(vec2(0, 2)), m = tap(vec2(2, 2));
    vec3 color;
    if (u.threshold.z > 0.5) {
        color = karis(d, e, i, j) * 0.5 + karis(a, b, f, g) * 0.125 + karis(b, c, g, h) * 0.125 +
                karis(f, g, k, l) * 0.125 + karis(g, h, l, m) * 0.125;
        float br = max(color.r, max(color.g, color.b));
        float soft = clamp(br - u.threshold.x + u.threshold.y, 0.0, 2.0 * u.threshold.y);
        soft = soft * soft / (4.0 * u.threshold.y + 1e-5);
        color *= max(soft, br - u.threshold.x) / max(br, 1e-5);
    } else {
        color = (d + e + i + j) * 0.125 + (a + c + k + m) * 0.03125 + (b + f + h + l) * 0.0625 + g * 0.125;
    }
    outColor = vec4(color, 1.0);
}
