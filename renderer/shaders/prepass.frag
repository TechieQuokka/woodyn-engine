#version 460
layout(location = 3) in vec3 vWorldNormal;
layout(location = 0) out vec4 outNormal;
void main() { outNormal = vec4(normalize(vWorldNormal), 1.0); }
