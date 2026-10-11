#version 450
layout(location = 0) in vec3 pos;
#if LOCATION_SPAN == 1
layout(location = 16) out mat4 occupiedMatrix;
layout(location = 20) out vec4 occupiedArray[2];
#elif LOCATION_SPAN == 2
layout(location = 16) out dvec3 occupiedWide;
layout(location = 18) out vec4 occupiedTail;
#elif LOCATION_SPAN == 3
out Occupied {
  layout(location = 16) mat2 matrix;
  layout(location = 18) vec4 values[2];
} occupiedBlock;
#endif
#if ACTIVE_CULL == 1
out gl_PerVertex { vec4 gl_Position; float gl_ClipDistance[6]; };
layout(location = 1) out float independentCull[2];
#endif
void main() {
#if LOCATION_SPAN == 1
  occupiedMatrix = mat4(1);
  occupiedArray[0] = occupiedArray[1] = vec4(pos, 1);
#elif LOCATION_SPAN == 2
  occupiedWide = dvec3(pos);
  occupiedTail = vec4(pos, 1);
#elif LOCATION_SPAN == 3
  occupiedBlock.matrix = mat2(1);
  occupiedBlock.values[0] = occupiedBlock.values[1] = vec4(pos, 1);
#endif
  gl_Position = vec4(pos, 1.0);
  gl_ClipDistance[0] = pos.x;
  gl_ClipDistance[1] = pos.y;
  gl_ClipDistance[2] = 1.0;
  gl_ClipDistance[3] = 1.0;
  gl_ClipDistance[4] = 1.0;
  gl_ClipDistance[5] = 1.0;
#if ACTIVE_CULL == 1
  independentCull[0] = pos.z;
  independentCull[1] = -pos.z;
#elif ACTIVE_CULL == 2
  gl_CullDistance[0] = pos.z;
  gl_CullDistance[1] = -pos.z;
#endif
}
