/* SPDX-License-Identifier: Apache-2.0 */
#define VK_NO_PROTOTYPES
#include "polygon_layouts.h"
#include "../layer/layer.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define H(t,n) ((t)(uintptr_t)(n))
static uint64_t generation=1;
static unsigned set_calls,layout_calls;
static unsigned allocations,fail_at,live;
static VKAPI_ATTR void *VKAPI_CALL allocate(void *user,size_t size,size_t alignment,VkSystemAllocationScope scope)
{ (void)user;(void)alignment;(void)scope;
 if(++allocations==fail_at) return NULL;
 void *p=malloc(size);if(p) ++live;return p; }
static VKAPI_ATTR void VKAPI_CALL release(void *user,void *p)
{ (void)user;if(p) {assert(live);--live;free(p);} }
int hybris_layer_lookup_device(VkDevice device,struct hybris_layer_device *out)
{ *out=(struct hybris_layer_device){.handle=device,.generation=generation};return 1; }
static VKAPI_ATTR VkResult VKAPI_CALL create_set(VkDevice d,const VkDescriptorSetLayoutCreateInfo *i,
 const VkAllocationCallbacks *a,VkDescriptorSetLayout *out)
{ (void)d;(void)i;(void)a; ++set_calls;*out=H(VkDescriptorSetLayout,1);return VK_SUCCESS; }
static VKAPI_ATTR VkResult VKAPI_CALL create_layout(VkDevice d,const VkPipelineLayoutCreateInfo *i,
 const VkAllocationCallbacks *a,VkPipelineLayout *out)
{ (void)d;(void)i;(void)a; ++layout_calls;*out=H(VkPipelineLayout,2);return VK_SUCCESS; }
static VKAPI_ATTR void VKAPI_CALL destroy_set(VkDevice d,VkDescriptorSetLayout h,const VkAllocationCallbacks *a)
{ (void)d;(void)h;(void)a; }
static VKAPI_ATTR void VKAPI_CALL destroy_layout(VkDevice d,VkPipelineLayout h,const VkAllocationCallbacks *a)
{ (void)d;(void)h;(void)a; }
PFN_vkVoidFunction hybris_layer_device_inner_proc(VkDevice d,const char *n)
{
 (void)d;
#define P(name,fn) if(!strcmp(n,"vk" #name)) return (PFN_vkVoidFunction)fn
 P(CreateDescriptorSetLayout,create_set);P(DestroyDescriptorSetLayout,destroy_set);
 P(CreatePipelineLayout,create_layout);P(DestroyPipelineLayout,destroy_layout);
#undef P
 assert(0);return NULL;
}
int main(void)
{
 PFN_vkCreateDescriptorSetLayout cs=(void*)hybris_polygon_layouts_proc("vkCreateDescriptorSetLayout");
 PFN_vkDestroyDescriptorSetLayout ds=(void*)hybris_polygon_layouts_proc("vkDestroyDescriptorSetLayout");
 PFN_vkCreatePipelineLayout cl=(void*)hybris_polygon_layouts_proc("vkCreatePipelineLayout");
 PFN_vkDestroyPipelineLayout dl=(void*)hybris_polygon_layouts_proc("vkDestroyPipelineLayout");
 VkDevice device=H(VkDevice,1);VkDescriptorSetLayout set;VkPipelineLayout layout;
 VkSampler samplers[]={H(VkSampler,3),H(VkSampler,4)};
 VkDescriptorSetLayoutBinding binding={.binding=7,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
  .descriptorCount=2,.stageFlags=VK_SHADER_STAGE_VERTEX_BIT,.pImmutableSamplers=samplers};
 VkDescriptorSetLayoutCreateInfo si={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
  .bindingCount=1,.pBindings=&binding};
 assert(cs(device,&si,NULL,&set)==VK_SUCCESS);
 samplers[0]=VK_NULL_HANDLE;binding.binding=99;
 VkPushConstantRange push={VK_SHADER_STAGE_VERTEX_BIT,0,32};
 VkPipelineLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
  .setLayoutCount=1,.pSetLayouts=&set,.pushConstantRangeCount=1,.pPushConstantRanges=&push};
 assert(cl(device,&li,NULL,&layout)==VK_SUCCESS);
 const struct hybris_polygon_layout *old=hybris_polygon_layout_acquire(device,layout);assert(old);
 push.size=4;
 ds(device,set,NULL);dl(device,layout,NULL);
 assert(!hybris_polygon_layout_acquire(device,layout));
 assert(old->info.pPushConstantRanges[0].size==32 && old->sets[0].pBindings[0].binding==7);
 assert(old->sets[0].pBindings[0].pImmutableSamplers[0]==H(VkSampler,3));
 /* Both driver handles are deliberately reused with new definitions. */
 binding.pImmutableSamplers=NULL;
 assert(cs(device,&si,NULL,&set)==VK_SUCCESS);assert(cl(device,&li,NULL,&layout)==VK_SUCCESS);
 const struct hybris_polygon_layout *fresh=hybris_polygon_layout_acquire(device,layout);assert(fresh && fresh!=old);
 assert(fresh->sets[0].pBindings[0].binding==99 && fresh->info.pPushConstantRanges[0].size==4);
 assert(old->sets[0].pBindings[0].binding==7);
 ++generation;assert(!hybris_polygon_layout_acquire(device,layout));
 --generation;
 hybris_polygon_layouts_release_device(device);
 assert(fresh->sets[0].pBindings[0].binding==99);
 hybris_polygon_layout_release(old);hybris_polygon_layout_release(fresh);
 /* Unknown declarations go to the real driver, without capturing dangling
  * extension pointers or failing an otherwise supported application layout. */
 VkBaseInStructure unknown={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO};si.pNext=&unknown;
 assert(cs(device,&si,NULL,&set)==VK_SUCCESS);assert(cl(device,&li,NULL,&layout)==VK_SUCCESS);
 assert(!hybris_polygon_layout_acquire(device,layout));
 ds(device,set,NULL);dl(device,layout,NULL);
 assert(set_calls==3 && layout_calls==3);
 /* Exercise every allocation failure in set and pipeline snapshots. No
  * partial snapshot or allocator-owned metadata may survive a failed create. */
 si.pNext=NULL;binding.pImmutableSamplers=samplers;
 VkAllocationCallbacks allocator={.pfnAllocation=allocate,.pfnFree=release};
 for(unsigned failure=1;failure<=4;++failure) {
   allocations=0;fail_at=failure;
   VkResult result=cs(device,&si,&allocator,&set);
   assert(result==VK_SUCCESS || result==VK_ERROR_OUT_OF_HOST_MEMORY);
   if(!result) ds(device,set,&allocator);
   assert(!live);
 }
 fail_at=0;allocations=0;assert(cs(device,&si,&allocator,&set)==VK_SUCCESS);
 unsigned set_live=live;
 for(unsigned failure=1;failure<=7;++failure) {
   allocations=0;fail_at=failure;
   VkResult result=cl(device,&li,&allocator,&layout);
   assert(result==VK_SUCCESS || result==VK_ERROR_OUT_OF_HOST_MEMORY);
   if(!result) dl(device,layout,&allocator);
   assert(live==set_live);
 }
 ds(device,set,&allocator);assert(!live);
 puts("PASS polygon layouts: deep copies, immutable samplers, destroyed/reused handles, retained snapshots, device generations, unsupported chains");
}
