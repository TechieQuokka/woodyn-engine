#ifndef WOODYN_FRAME_GLSL
#define WOODYN_FRAME_GLSL
// Must match render::FrameUniforms (std140).
layout(std140, set = 3, binding = 0) uniform Frame {
    mat4 lightViewProj;
    vec4 eye;       // xyz
    vec4 sunDir;    // xyz: direction toward the sun
    vec4 sunColor;  // rgb: radiance
    vec4 sh[9];     // irradiance SH (pre-convolved)
    vec4 viewport;  // w, h, 1/w, 1/h
    vec4 params;    // x: AO on, y: contact shadows on, z: env max mip, w: shadow texel size
} frame;
#endif
