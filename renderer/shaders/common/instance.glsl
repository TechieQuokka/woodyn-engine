#ifndef WOODYN_INSTANCE_GLSL
#define WOODYN_INSTANCE_GLSL
// Must match render::GpuInstance (std430, 128 bytes).
struct Instance {
    mat4 model;
    vec4 cutRot;    // block-local -> log rotation (x, y, z, w)
    vec4 cutCenter; // xyz: block origin in log space, w: wood source index
    vec4 stain;     // rgb: dye color (linear), w: 1 = stained
    vec4 misc;      // x: highlight, y: finish (0 toy lacquer, 1 floor varnish)
};
#endif
