/* SPDX-License-Identifier: Apache-2.0 */
#define VK_NO_PROTOTYPES
#include "bc_command_state.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define HANDLE(type,n) ((type)(uintptr_t)(n))
static unsigned clones, destroyed, events;
static VkPipelineLayout expected_layout;
static VkPipelineBindPoint expected_point;
static VkPipeline expected_pipeline;
static const struct hybris_bc_layout *expected_identity;
static unsigned visits;
static VkResult visit_descriptors(void *user,const struct hybris_bc_layout *identity,
    uint32_t first,uint32_t count,const VkDescriptorSet *sets,uint32_t n,const uint32_t *offsets)
{
    (void)user;assert(identity==expected_identity && visits++==0);
    assert(first==2 && count==1 && sets[0]==HANDLE(VkDescriptorSet,9));
    assert(n==2 && offsets[0]==256 && offsets[1]==512);return VK_SUCCESS;
}
static VkResult visit_push(void *user,const struct hybris_bc_layout *identity,
    VkShaderStageFlags stages,uint32_t offset,uint32_t size,const void *values)
{
    (void)user;assert(identity==expected_identity && visits++==1);
    assert(stages==(VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_COMPUTE_BIT));
    assert(offset==0 && size==4 && *(const uint32_t*)values==17);return VK_SUCCESS;
}
static VkResult reject_descriptors(void *user,const struct hybris_bc_layout *identity,
    uint32_t first,uint32_t count,const VkDescriptorSet *sets,uint32_t n,const uint32_t *offsets)
{
    (void)user;(void)identity;(void)first;(void)count;(void)sets;(void)n;(void)offsets;
    ++visits;return VK_ERROR_FEATURE_NOT_PRESENT;
}
static VKAPI_ATTR VkResult VKAPI_CALL create(VkDevice d,const VkPipelineLayoutCreateInfo *i,
    const VkAllocationCallbacks *a,VkPipelineLayout *out)
{ (void)d;(void)a; assert(i->sType==VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
  *out=HANDLE(VkPipelineLayout,100+ ++clones); return VK_SUCCESS; }
static VKAPI_ATTR void VKAPI_CALL destroy(VkDevice d,VkPipelineLayout l,const VkAllocationCallbacks *a)
{ (void)d;(void)a; assert(l); ++destroyed; }
static VKAPI_ATTR void VKAPI_CALL pipeline(VkCommandBuffer c,VkPipelineBindPoint p,VkPipeline h)
{ (void)c; assert(events++==0 && p==expected_point && h==expected_pipeline); }
static VKAPI_ATTR void VKAPI_CALL descriptors(VkCommandBuffer c,VkPipelineBindPoint p,
    VkPipelineLayout l,uint32_t first,uint32_t count,const VkDescriptorSet *sets,
    uint32_t n,const uint32_t *offsets)
{ (void)c; assert(events++==1 && p==expected_point && l==expected_layout);
  assert(first==2 && count==1 && sets[0]==HANDLE(VkDescriptorSet,9));
  assert(n==2 && offsets[0]==256 && offsets[1]==512); }
static VKAPI_ATTR void VKAPI_CALL push(VkCommandBuffer c,VkPipelineLayout l,
    VkShaderStageFlags stages,uint32_t offset,uint32_t size,const void *value)
{ (void)c; assert(l==expected_layout && size==4);
  int mixed=expected_point==VK_PIPELINE_BIND_POINT_COMPUTE ? events==3 : events==2;
  if(mixed) assert(stages==(VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_COMPUTE_BIT) && offset==0 && *(const uint32_t*)value==17);
  else assert(stages==VK_SHADER_STAGE_FRAGMENT_BIT && offset==4 && *(const uint32_t*)value==23);
  ++events; }
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL resolve(VkDevice d,const char *name)
{ (void)d;
#define PROC(n,f) if(!strcmp(name,"vk" #n)) return (PFN_vkVoidFunction)f
  PROC(CreatePipelineLayout,create); PROC(DestroyPipelineLayout,destroy);
  PROC(CmdBindPipeline,pipeline); PROC(CmdBindDescriptorSets,descriptors); PROC(CmdPushConstants,push);
#undef PROC
  assert(0); return NULL; }
int main(void)
{
  struct hybris_bc_layouts layouts;
  struct hybris_bc_command_state state;
  VkPipelineLayout original=HANDLE(VkPipelineLayout,1);
  VkPipelineLayoutCreateInfo info={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  VkDescriptorSet set=HANDLE(VkDescriptorSet,9);
  uint32_t offsets[]={256,512}, values[]={17,23};
  hybris_bc_layouts_init(&layouts,HANDLE(VkDevice,1),resolve);
  assert(hybris_bc_layout_add(&layouts,original,&info)==VK_SUCCESS);
  struct hybris_bc_layout *identity=NULL;
  assert(hybris_bc_layout_acquire(&layouts,original,&identity)==VK_SUCCESS);
  expected_identity=identity;
  hybris_bc_command_state_init(&state,&layouts);
  /* Default BC-only capture neither allocates nor requires graphics layouts. */
  hybris_bc_save_pipeline(&state,VK_PIPELINE_BIND_POINT_GRAPHICS,HANDLE(VkPipeline,6));
  hybris_bc_save_descriptors(&state,VK_PIPELINE_BIND_POINT_GRAPHICS,HANDLE(VkPipelineLayout,999),2,1,&set,2,offsets);
  assert(!state.first && !state.graphics_pipeline && state.error==VK_SUCCESS);
  hybris_bc_command_state_track_graphics(&state);
  for(unsigned p=0;p<2;++p) {
    VkPipelineBindPoint point=p?VK_PIPELINE_BIND_POINT_COMPUTE:VK_PIPELINE_BIND_POINT_GRAPHICS;
    hybris_bc_save_pipeline(&state,point,HANDLE(VkPipeline,6+p));
    hybris_bc_save_descriptors(&state,point,original,2,1,&set,2,offsets);
  }
  hybris_bc_save_push(&state,original,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_COMPUTE_BIT,0,4,&values[0]);
  hybris_bc_save_push(&state,original,VK_SHADER_STAGE_FRAGMENT_BIT,4,4,&values[1]);
  /* Application storage may change; destroyed/reused handles must not replace
   * the retained layout clone in already recorded commands. */
  offsets[0]=0; values[0]=0; set=VK_NULL_HANDLE;
  hybris_bc_layout_remove(&layouts,original); assert(!destroyed);
  assert(hybris_bc_layout_add(&layouts,original,&info)==VK_SUCCESS);
  expected_layout=HANDLE(VkPipelineLayout,101);
  struct hybris_bc_graphics_visitor visitor={NULL,visit_descriptors,visit_push};
  visits=0;assert(hybris_bc_visit_graphics(&state,&visitor)==VK_SUCCESS && visits==2);
  visitor.descriptors=reject_descriptors;
  visits=0;assert(hybris_bc_visit_graphics(&state,&visitor)==VK_ERROR_FEATURE_NOT_PRESENT && visits==1);
  assert(state.error==VK_SUCCESS);
  for(unsigned p=0;p<2;++p) {
    events=0; expected_point=p?VK_PIPELINE_BIND_POINT_COMPUTE:VK_PIPELINE_BIND_POINT_GRAPHICS;
    expected_pipeline=HANDLE(VkPipeline,6+p);
    if(p) hybris_bc_restore_compute(&state,HANDLE(VkCommandBuffer,1));
    else hybris_bc_restore_graphics(&state,HANDLE(VkCommandBuffer,1));
    assert(events==4 && state.error==VK_SUCCESS);
  }
  hybris_bc_layout_release(&layouts,identity);
  hybris_bc_command_state_reset(&state);
  assert(destroyed==1 && state.track_graphics && !state.first && !state.graphics_pipeline && !state.compute_pipeline);
  hybris_bc_save_push(&state,HANDLE(VkPipelineLayout,999),VK_SHADER_STAGE_VERTEX_BIT,0,4,values);
  assert(state.error==VK_ERROR_INITIALIZATION_FAILED);
  events=0; hybris_bc_restore_graphics(&state,HANDLE(VkCommandBuffer,1)); assert(!events);
  hybris_bc_command_state_reset(&state); assert(state.error==VK_SUCCESS);
  hybris_bc_layouts_finish(&layouts); assert(destroyed==2);
  puts("PASS command state: graphics/compute isolation, ordered push ranges, dynamic offsets, retained layouts, reset and recording errors");
}
