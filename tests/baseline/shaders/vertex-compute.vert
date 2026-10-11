// SPDX-License-Identifier: Apache-2.0
#version 450
#ifdef INPUT_INSTANCE
#extension GL_ARB_shader_draw_parameters : require
#endif
#ifdef PHYSICAL_MEMORY
#extension GL_EXT_buffer_reference : require
#pragma use_vulkan_memory_model
layout(buffer_reference,std430) readonly buffer VertexBias { vec4 bias; };
#endif
#ifdef INPUT_MATRIX
layout(location=0) in mat4 value;
#elif defined(INPUT_VEC3)
layout(location=0) in vec3 value;
#else
layout(location=0) in vec4 value;
#endif
#if !defined(FULL_BLOCK) && !defined(ACTIVE_CLIP)
out gl_PerVertex { vec4 gl_Position; };
#endif
layout(location=0) out vec4 color;
layout(location=1) out vec4 extraVarying;
layout(push_constant) uniform Transform {
    vec4 scale; vec4 offset;
#ifdef PHYSICAL_MEMORY
    VertexBias vertexBias;
#endif
} transform;
#ifdef RESOURCE_COLLISION
layout(set=0,binding=0) uniform Resource { vec4 bias; } parameters;
#endif
#ifdef RESOURCE_PRESENT
layout(set=1,binding=0) uniform Resource { vec4 bias; } parameters;
#endif
#if defined(EXTERNAL_STORE) || defined(EXTERNAL_ATOMIC)
layout(set=1,binding=0,std430) buffer Effects { uint count; } effects;
#endif
void main() {
#ifdef ACTIVE_CLIP
    gl_ClipDistance[0]=value.x;
#endif
#ifdef EXTERNAL_STORE
    effects.count=uint(gl_VertexIndex);
#endif
#ifdef EXTERNAL_ATOMIC
    atomicAdd(effects.count,1u);
#endif
#ifdef INPUT_MATRIX
    gl_Position=value[0]*transform.scale+transform.offset;
#elif defined(INPUT_VEC3)
    gl_Position=vec4(value,1.0)*transform.scale+transform.offset;
#else
    gl_Position=value*transform.scale+transform.offset;
#endif
    vec4 localColor=vec4(float((gl_VertexIndex+1)&1),0.5,1.0,1.0);
    color=localColor;
#ifdef INPUT_MATRIX
    extraVarying=vec4(value[0].xyz,2.0);
#else
    extraVarying=vec4(value.xyz,2.0);
#endif
#ifdef INPUT_INSTANCE
    color.y+=float(gl_InstanceIndex)*.02+float(gl_BaseInstanceARB)*.01+float(gl_BaseVertexARB)*.001;
#endif
#if defined(RESOURCE_COLLISION) || defined(RESOURCE_PRESENT)
    gl_Position+=parameters.bias;
#endif
#ifdef PHYSICAL_MEMORY
    gl_Position+=transform.vertexBias.bias;
#endif
}
