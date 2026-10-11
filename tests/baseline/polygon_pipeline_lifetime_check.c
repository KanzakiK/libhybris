/* SPDX-License-Identifier: Apache-2.0 */
#define VK_NO_PROTOTYPES
#include "polygon_pipeline.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define H(t,n) ((t)(uintptr_t)(n))
static unsigned destroyed,identities,layouts;
static struct hybris_bc_layouts journal;
static struct hybris_polygon_layout declaration;
/* Exercise callback reentry: destruction must not hold the registry guard. */
static void reenter(VkDevice device)
{
 struct hybris_polygon_pipeline *p=hybris_polygon_pipeline_acquire(device,H(VkPipeline,42));
 if(p) hybris_polygon_pipeline_release(p);
}
void hybris_bc_layout_release(struct hybris_bc_layouts *l,struct hybris_bc_layout *i)
{assert(l==&journal && i);++identities;reenter(H(VkDevice,1));}
void hybris_polygon_layout_release(const struct hybris_polygon_layout *l)
{assert(l==&declaration);++layouts;reenter(H(VkDevice,1));}
static VKAPI_ATTR void VKAPI_CALL destroy_pipeline(VkDevice d,VkPipeline p,const VkAllocationCallbacks *a)
{assert(p && !a);++destroyed;reenter(d);}
static VKAPI_ATTR void VKAPI_CALL destroy_layout(VkDevice d,VkPipelineLayout l,const VkAllocationCallbacks *a)
{assert(l && !a);reenter(d);}
static VKAPI_ATTR void VKAPI_CALL destroy_set(VkDevice d,VkDescriptorSetLayout l,const VkAllocationCallbacks *a)
{assert(l && !a);reenter(d);}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL resolve(VkDevice d,const char *name)
{(void)d;
 if(!strcmp(name,"vkDestroyPipeline")) return (PFN_vkVoidFunction)destroy_pipeline;
 if(!strcmp(name,"vkDestroyPipelineLayout")) return (PFN_vkVoidFunction)destroy_layout;
 if(!strcmp(name,"vkDestroyDescriptorSetLayout")) return (PFN_vkVoidFunction)destroy_set;
 assert(0);return NULL;}
static struct hybris_polygon_pipeline *create(VkDevice device)
{
 struct hybris_polygon_pipeline *p=calloc(1,sizeof(*p));assert(p);
 p->device=device;p->resolver=resolve;p->filled=H(VkPipeline,42);
 p->producer=H(VkPipeline,1);p->lines=H(VkPipeline,2);p->points=H(VkPipeline,3);
 p->layout=H(VkPipelineLayout,4);p->sets=malloc(sizeof(*p->sets));assert(p->sets);
 p->sets[0]=H(VkDescriptorSetLayout,5);
 return p;
}
static VkResult publish(struct hybris_polygon_pipeline *p)
{return hybris_polygon_pipeline_publish(p,&declaration,&journal,(void*)(uintptr_t)1);}
int main(void)
{
 VkDevice device=H(VkDevice,1),other=H(VkDevice,2);
 struct hybris_polygon_pipeline *old=create(device);
 assert(publish(old)==VK_SUCCESS);
 assert(publish(old)==VK_ERROR_INITIALIZATION_FAILED && old->references==1);
 struct hybris_polygon_pipeline *command=hybris_polygon_pipeline_acquire(device,old->filled);
 assert(command==old && old->references==2 && !hybris_polygon_pipeline_acquire(other,old->filled));
 assert(hybris_polygon_pipeline_remove(device,old->filled) && !destroyed);
 assert(!hybris_polygon_pipeline_acquire(device,old->filled));
 struct hybris_polygon_pipeline *fresh=create(device);
 assert(publish(fresh)==VK_SUCCESS);
 assert(hybris_polygon_pipeline_acquire(device,fresh->filled)==fresh);
 hybris_polygon_pipeline_release(fresh);
 hybris_polygon_pipeline_release(command);
 assert(destroyed==4 && identities==1 && layouts==1 && fresh->references==1);
 assert(hybris_polygon_pipeline_remove(device,fresh->filled));
 assert(destroyed==8 && identities==2 && layouts==2);
 assert(!hybris_polygon_pipeline_remove(device,H(VkPipeline,42)));
 struct hybris_polygon_pipeline *a=create(device),*b=create(other);
 assert(publish(a)==VK_SUCCESS && publish(b)==VK_SUCCESS);
 command=hybris_polygon_pipeline_acquire(device,a->filled);
 hybris_polygon_pipelines_release_device(device);
 assert(destroyed==8 && !hybris_polygon_pipeline_acquire(device,H(VkPipeline,42)));
 hybris_polygon_pipeline_release(command);assert(destroyed==12);
 assert(hybris_polygon_pipeline_acquire(other,H(VkPipeline,42))==b);
 hybris_polygon_pipeline_release(b);
 hybris_polygon_pipelines_release_device(other);
 assert(destroyed==16 && identities==4 && layouts==4);
 puts("PASS polygon pipeline lifetime: command retention, removed/reused handles, device isolation, exactly-once cleanup, callbacks outside guard");
}
