#version 460
layout(location = 0) out vec2 vUV;
void main() {
    // One triangle covering the screen. SDL GPU: NDC +Y up, texture uv (0,0) top-left.
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vUV = vec2(p.x, 1.0 - p.y);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
