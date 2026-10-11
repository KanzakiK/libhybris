/* SPDX-License-Identifier: Apache-2.0 */
#define VK_NO_PROTOTYPES
#include "polygon_raster.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define H(t,n) ((t)(uintptr_t)(n))
static unsigned step;
static VkBool32 indexed;
static VkPolygonMode mode;
static VkResult binding_result;
static VkIndexType source_type;
static struct hybris_bc_command_state journal;
VkResult hybris_polygon_bindings_bind(const struct hybris_polygon_bindings *b,
    VkCommandBuffer c,VkPipelineBindPoint p)
{
    assert(b && c && p==VK_PIPELINE_BIND_POINT_GRAPHICS && step==0);
    if(binding_result) return binding_result;
    ++step;return VK_SUCCESS;
}
void hybris_bc_restore_graphics(struct hybris_bc_command_state *s,VkCommandBuffer c)
{assert(s==&journal && c && step==5);++step;}
static VKAPI_ATTR void VKAPI_CALL pipeline(VkCommandBuffer c,VkPipelineBindPoint p,VkPipeline h)
{assert(c && p==VK_PIPELINE_BIND_POINT_GRAPHICS && step==1);
 assert(h==H(VkPipeline,mode==VK_POLYGON_MODE_LINE ? 2 : 3));++step;}
static VKAPI_ATTR void VKAPI_CALL sets(VkCommandBuffer c,VkPipelineBindPoint p,VkPipelineLayout l,
    uint32_t first,uint32_t n,const VkDescriptorSet *s,uint32_t nd,const uint32_t *d)
{assert(c && p==VK_PIPELINE_BIND_POINT_GRAPHICS && l==H(VkPipelineLayout,1));
 assert(step==2 && first==3 && n==1 && *s==H(VkDescriptorSet,1) && !nd && !d);++step;}
static VKAPI_ATTR void VKAPI_CALL index_buffer(VkCommandBuffer c,VkBuffer b,VkDeviceSize o,VkIndexType t)
{assert(c);
 if(step==3) assert(b==H(VkBuffer,1) && !o && t==VK_INDEX_TYPE_UINT32);
 else assert(step==7 && b==H(VkBuffer,4) && o==12 && t==source_type);
 ++step;}
static VKAPI_ATTR void VKAPI_CALL vertices(VkCommandBuffer c,uint32_t first,uint32_t n,
    const VkBuffer *b,const VkDeviceSize *o)
{assert(c && step==6 && first==7 && n==1 && *b==H(VkBuffer,3) && *o==8);++step;}
static VKAPI_ATTR void VKAPI_CALL indexed_draw(VkCommandBuffer c,VkBuffer b,VkDeviceSize o,
    uint32_t n,uint32_t stride)
{assert(c && b==H(VkBuffer,2) && n==1 && stride==sizeof(VkDrawIndexedIndirectCommand));
 if(step==4) assert(o==0);
 else assert(step==8 && indexed && o==sizeof(VkDrawIndexedIndirectCommand));
 ++step;}
static VKAPI_ATTR void VKAPI_CALL direct_draw(VkCommandBuffer c,VkBuffer b,VkDeviceSize o,
    uint32_t n,uint32_t stride)
{assert(c && step==8 && !indexed && b==H(VkBuffer,2));
 assert(o==sizeof(VkDrawIndexedIndirectCommand) && n==1 && stride==sizeof(VkDrawIndirectCommand));++step;}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL resolve(VkDevice d,const char *name)
{assert(d==H(VkDevice,1));
#define P(n,f) if(!strcmp(name,"vk" #n)) return (PFN_vkVoidFunction)f
 P(CmdBindPipeline,pipeline);P(CmdBindDescriptorSets,sets);P(CmdBindIndexBuffer,index_buffer);
 P(CmdBindVertexBuffers,vertices);P(CmdDrawIndexedIndirect,indexed_draw);P(CmdDrawIndirect,direct_draw);
#undef P
 assert(0);return NULL;}
int main(void)
{
 struct hybris_polygon_pipeline p={.device=H(VkDevice,1),.resolver=resolve,
    .lines=H(VkPipeline,2),.points=H(VkPipeline,3),.filled=H(VkPipeline,4),
    .layout=H(VkPipelineLayout,1),.reserved_set=3};
 VkBuffer vb=H(VkBuffer,3);VkDeviceSize offset=8;
 struct hybris_polygon_source s={.first_binding=7,.binding_count=1,.vertices=&vb,.offsets=&offset,
    .indices=H(VkBuffer,4),.index_offset=12};
 journal.track_graphics=1;journal.graphics_pipeline=p.filled;
 const struct hybris_polygon_bindings *bindings=(const void *)(uintptr_t)1;
#define RECORD() hybris_polygon_raster_record(&p,bindings,&journal,H(VkCommandBuffer,1),mode, \
    H(VkDescriptorSet,1),H(VkBuffer,1),H(VkBuffer,2),&s)
 for(unsigned m=1;m<=2;++m) for(unsigned i=0;i<=1;++i) for(unsigned t=0;t<=1;++t) {
    mode=(VkPolygonMode)m;indexed=i;s.indexed=i;
    source_type=t ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;s.index_type=source_type;
    step=0;assert(RECORD()==VK_SUCCESS && step==9);
 }
 step=0;journal.track_graphics=0;
 assert(RECORD()==VK_ERROR_FEATURE_NOT_PRESENT && !step);
 journal.track_graphics=1;journal.graphics_pipeline=H(VkPipeline,99);
 assert(RECORD()==VK_ERROR_FEATURE_NOT_PRESENT && !step);
 journal.graphics_pipeline=p.filled;mode=VK_POLYGON_MODE_FILL;
 assert(RECORD()==VK_ERROR_FEATURE_NOT_PRESENT && !step);
 mode=VK_POLYGON_MODE_LINE;s.index_type=VK_INDEX_TYPE_UINT8_EXT;
 assert(RECORD()==VK_ERROR_FEATURE_NOT_PRESENT && !step);
 s.index_type=source_type;binding_result=VK_ERROR_OUT_OF_HOST_MEMORY;
 assert(RECORD()==binding_result && !step);
 puts("PASS polygon raster: GPU-selected two draws, restore before original fallback, direct/indexed offsets, pre-command rejection");
}
