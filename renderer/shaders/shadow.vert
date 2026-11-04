#version 460
#include "instance.glsl"
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(std430, set = 0, binding = 0) readonly buffer Instances { Instance instances[]; };
layout(std140, set = 1, binding = 0) uniform VS { mat4 lightViewProj; } vs;
void main() {
    gl_Position = vs.lightViewProj * (instances[gl_InstanceIndex].model * vec4(inPosition, 1.0));
}
