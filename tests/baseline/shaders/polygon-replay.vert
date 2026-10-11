// SPDX-License-Identifier: Apache-2.0
#version 450
struct Record { vec4 position; vec4 color; };
layout(set=0,binding=0,std430) readonly buffer Records { Record vertex[]; } records;
layout(location=0) out vec4 color;
void main() {
    gl_Position=records.vertex[gl_VertexIndex].position;
    color=records.vertex[gl_VertexIndex].color;
    gl_PointSize=1.0;
}
