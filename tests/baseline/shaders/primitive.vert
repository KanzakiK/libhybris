#version 450
#ifdef CULL_BLOCK
out gl_PerVertex { vec4 gl_Position; float gl_CullDistance[2]; };
#define distances gl_CullDistance
#else
out gl_PerVertex { vec4 gl_Position; };
#endif
layout(location=0) in vec4 value;
layout(location=0) flat out vec4 color;
// User varyings model the unsupported CullDistance output after translation.
#ifndef CULL_BLOCK
layout(location=1) out float distances[2];
#endif
layout(push_constant) uniform Expected { vec4 value; int mode; } expected;
void main() {
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
    // Keep edge-fixture positions in the producer: the adapter must preserve
    // application positions, not replace them with a geometry-stage triangle.
    int mode = expected.mode & 255;
    if ((expected.mode & 256) != 0)
        gl_Position = vec4(p * 0.5 - 0.5, 0.0, 1.0);
    color = vec4(equal(value, expected.value));
    distances[0] = mode == 1 ? -1.0 : 1.0;
    distances[1] = mode == 3 ? -1.0 : 1.0;
    if (mode == 2 || mode == 4) {
        distances[0] = gl_VertexIndex != 2 ? -1.0 : 1.0;
        distances[1] = gl_VertexIndex == 2 ? -1.0 : 1.0;
    }
    if (mode == 5) { distances[0] = 0.0; distances[1] = 0.0; }
}
