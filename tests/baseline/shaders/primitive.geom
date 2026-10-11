#version 450
in gl_PerVertex { vec4 gl_Position; } gl_in[];
out gl_PerVertex { vec4 gl_Position; };
layout(triangles) in;
#ifdef PRIMITIVE_LINES
layout(line_strip, max_vertices=6) out;
#else
layout(triangle_strip, max_vertices=3) out;
#endif
layout(location=0) flat in vec4 colors[];
layout(location=1) in float distances[][2];
layout(location=0) flat out vec4 color;
void main() {
    for (int plane=0; plane<2; ++plane)
        if (distances[0][plane] < 0 && distances[1][plane] < 0 && distances[2][plane] < 0) return;
#ifdef PRIMITIVE_LINES
    for (int edge=0; edge<3; ++edge) {
        for (int end=0; end<2; ++end) {
            int i = (edge+end)%3;
            gl_Position = gl_in[i].gl_Position;
            color = colors[i];
            EmitVertex();
        }
        EndPrimitive();
    }
#else
    for (int i=0; i<3; ++i) {
        gl_Position = gl_in[i].gl_Position;
        color = colors[i];
        EmitVertex();
    }
    EndPrimitive();
#endif
}
