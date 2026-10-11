/* SPDX-License-Identifier: Apache-2.0 */
#define VK_NO_PROTOTYPES
#include "polygon_indices.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define H(t,n) ((t)(uintptr_t)(n))
static unsigned calls;
static struct hybris_polygon_push expected;
static VKAPI_ATTR void VKAPI_CALL bind_pipeline(VkCommandBuffer c,VkPipelineBindPoint p,VkPipeline h)
{(void)c;assert(p==VK_PIPELINE_BIND_POINT_COMPUTE && h==H(VkPipeline,1));++calls;}
static VKAPI_ATTR void VKAPI_CALL bind_sets(VkCommandBuffer c,VkPipelineBindPoint p,VkPipelineLayout l,
 uint32_t first,uint32_t n,const VkDescriptorSet *s,uint32_t offsets,const uint32_t *o)
{(void)c;(void)l;(void)o;assert(p==VK_PIPELINE_BIND_POINT_COMPUTE && !first && n==1 && s[0]==H(VkDescriptorSet,1) && !offsets);++calls;}
static VKAPI_ATTR void VKAPI_CALL push(VkCommandBuffer c,VkPipelineLayout l,VkShaderStageFlags f,
 uint32_t offset,uint32_t size,const void *data)
{(void)c;(void)l;assert(f==VK_SHADER_STAGE_COMPUTE_BIT && !offset && size==sizeof(expected));assert(!memcmp(data,&expected,size));++calls;}
static VKAPI_ATTR void VKAPI_CALL dispatch(VkCommandBuffer c,uint32_t x,uint32_t y,uint32_t z)
{(void)c;assert(x==1 && y==1 && z==1);++calls;}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL resolve(VkDevice d,const char *name)
{(void)d;
#define P(n,f) if(!strcmp(name,"vk" #n)) return (PFN_vkVoidFunction)f
 P(CmdBindPipeline,bind_pipeline);P(CmdBindDescriptorSets,bind_sets);P(CmdPushConstants,push);P(CmdDispatch,dispatch);
#undef P
 assert(0);return NULL;}
int main(void)
{
 _Static_assert(sizeof(struct hybris_polygon_push)==52,"GPU push ABI");
 _Static_assert(offsetof(struct hybris_polygon_push,filled)==32,"GPU original arguments offset");
 struct hybris_polygon_indices converter={.device=H(VkDevice,1),.resolver=resolve,
  .pipeline=H(VkPipeline,1),.layout=H(VkPipelineLayout,1)};
 struct hybris_polygon_push p={.triangle_count=2,.mode=1,.record_count=6,.capacity=12,.record_stride=3,
  .filled={.indexCount=3,.instanceCount=2,.firstIndex=7,.vertexOffset=-2,.firstInstance=5}};
 expected=p;
 assert(hybris_polygon_indices_record(&converter,H(VkCommandBuffer,1),H(VkDescriptorSet,1),&p)==VK_SUCCESS && calls==4);
 calls=0;p.filled.indexCount=4;
 assert(hybris_polygon_indices_record(&converter,H(VkCommandBuffer,1),H(VkDescriptorSet,1),&p)==VK_ERROR_FEATURE_NOT_PRESENT && !calls);
 p=expected;p.triangle_count=3;
 assert(hybris_polygon_indices_record(&converter,H(VkCommandBuffer,1),H(VkDescriptorSet,1),&p)==VK_ERROR_FEATURE_NOT_PRESENT && !calls);
 p=expected;p.triangle_count=UINT32_MAX;
 assert(hybris_polygon_indices_record(&converter,H(VkCommandBuffer,1),H(VkDescriptorSet,1),&p)==VK_ERROR_FEATURE_NOT_PRESENT && !calls);
 p=expected;p.mode=0;
 assert(hybris_polygon_indices_record(&converter,H(VkCommandBuffer,1),H(VkDescriptorSet,1),&p)==VK_ERROR_FEATURE_NOT_PRESENT && !calls);
 puts("PASS polygon indirect arguments: original offsets/instances, exact push ABI, complete per-instance triangles, pre-command rejection");
}
