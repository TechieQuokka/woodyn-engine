#version 460
// 3x3 tent upsample, added onto the next larger mip (additive blending).
layout(location = 0) in vec2 vUV;
layout(set = 2, binding = 0) uniform sampler2D src;
layout(std140, set = 3, binding = 0) uniform Params { vec4 texel; } u; // texel.xy of src, .z radius
layout(location = 0) out vec4 outColor;
void main() {
    vec2 r = u.texel.xy * u.texel.z;
    vec3 c = texture(src, vUV + vec2(-r.x, -r.y)).rgb + texture(src, vUV + vec2(0, -r.y)).rgb * 2.0 +
             texture(src, vUV + vec2(r.x, -r.y)).rgb + texture(src, vUV + vec2(-r.x, 0)).rgb * 2.0 +
             texture(src, vUV).rgb * 4.0 + texture(src, vUV + vec2(r.x, 0)).rgb * 2.0 +
             texture(src, vUV + vec2(-r.x, r.y)).rgb + texture(src, vUV + vec2(0, r.y)).rgb * 2.0 +
             texture(src, vUV + vec2(r.x, r.y)).rgb;
    outColor = vec4(c / 16.0, 1.0);
}
