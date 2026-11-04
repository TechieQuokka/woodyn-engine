#version 460
layout(location = 2) in vec3 vWorldPos;
layout(location = 3) in vec3 vWorldNormal;
layout(std140, set = 3, binding = 0) uniform Ghost { vec4 eye; vec4 color; } ghost;
layout(location = 0) out vec4 outColor;
void main() {
    vec3 V = normalize(ghost.eye.xyz - vWorldPos);
    float rim = pow(1.0 - abs(dot(normalize(vWorldNormal), V)), 2.0);
    outColor = vec4(ghost.color.rgb * (0.6 + 0.8 * rim), ghost.color.a * (0.5 + 0.5 * rim));
}
