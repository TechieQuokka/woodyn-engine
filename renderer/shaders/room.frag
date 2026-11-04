#version 460
#include "frame.glsl"
#include "noise.glsl"
#include "lighting.glsl"

layout(location = 2) in vec3 vWorldPos;
layout(location = 3) in vec3 vWorldNormal;
layout(location = 5) in vec4 vAlbedoRough;
layout(location = 6) flat in float vMaterial;

layout(location = 0) out vec4 outColor;

void main() {
    Surface s;
    s.albedo = vAlbedoRough.rgb;
    s.roughness = vAlbedoRough.a;
    s.Ngeo = normalize(vWorldNormal);
    s.N = s.Ngeo;
    s.coat = 0.0;
    s.coatRoughness = 0.5;
    if (vMaterial > 0.5 && vMaterial < 1.5) {
        // EVA foam: fine closed-cell speckle and a soft orange-peel relief.
        float speckle = wd_gradientNoise(vWorldPos * 900.0, 77u);
        s.albedo *= 1.0 + 0.04 * speckle;
        float hx = wd_gradientNoise(vWorldPos * 350.0 + vec3(0.37, 0, 0), 78u);
        float hz = wd_gradientNoise(vWorldPos * 350.0 + vec3(0, 0, 0.61), 79u);
        s.N = normalize(s.Ngeo + vec3(hx, 0.0, hz) * 0.06);
        // Interlocking tile seams every 30 cm: a ~1 mm groove that catches shadow.
        vec2 g = abs(fract(vWorldPos.xz / 0.3 + 0.5) - 0.5) * 0.3; // distance to the seam [m]
        vec2 aa = max(fwidth(vWorldPos.xz), vec2(1e-5));
        float seam = 1.0 - smoothstep(0.0005, 0.0005 + aa.x * 1.5, g.x) * smoothstep(0.0005, 0.0005 + aa.y * 1.5, g.y);
        s.albedo *= 1.0 - 0.45 * seam;
        s.roughness = mix(s.roughness, 1.0, seam);
    }
    outColor = vec4(shade(s, vWorldPos), 1.0);
}
