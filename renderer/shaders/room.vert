#version 460
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inAlbedoRough;
layout(location = 3) in float inMaterial;

layout(std140, set = 1, binding = 0) uniform VS { mat4 viewProj; } vs;

layout(location = 2) out vec3 vWorldPos;
layout(location = 3) out vec3 vWorldNormal;
layout(location = 5) out vec4 vAlbedoRough;
layout(location = 6) flat out float vMaterial;

void main() {
    vWorldPos = inPosition;
    vWorldNormal = inNormal;
    vAlbedoRough = inAlbedoRough;
    vMaterial = inMaterial;
    gl_Position = vs.viewProj * vec4(inPosition, 1.0);
}
