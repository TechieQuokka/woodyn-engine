#version 460
#include "instance.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(std430, set = 0, binding = 0) readonly buffer Instances { Instance instances[]; };
layout(std140, set = 1, binding = 0) uniform VS { mat4 viewProj; } vs;

layout(location = 0) out vec3 vLocalPos;
layout(location = 1) out vec3 vLocalNormal;
layout(location = 2) out vec3 vWorldPos;
layout(location = 3) out vec3 vWorldNormal;
layout(location = 4) flat out uint vInstance;

void main() {
    Instance inst = instances[gl_InstanceIndex];
    vec4 world = inst.model * vec4(inPosition, 1.0);
    vLocalPos = inPosition;
    vLocalNormal = inNormal;
    vWorldPos = world.xyz;
    vWorldNormal = mat3(inst.model) * inNormal;
    vInstance = uint(gl_InstanceIndex);
    gl_Position = vs.viewProj * world;
}
