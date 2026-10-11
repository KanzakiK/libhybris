/* SPDX-License-Identifier: Apache-2.0 */
#define VK_NO_PROTOTYPES
#include "polygon_layouts.h"
#include "scaled_vertex.h"
#include "../layer/layer.h"
#include <pthread.h>
#include <string.h>

struct set_entry {
    VkDevice device; uint64_t generation;
    VkDescriptorSetLayout handle;
    VkDescriptorSetLayoutCreateInfo info;
    VkAllocationCallbacks allocator; unsigned custom;
    struct set_entry *next;
};
struct layout_entry {
    struct hybris_polygon_layout snapshot;
    VkDevice device; uint64_t generation;
    VkPipelineLayout handle;
    size_t references;
    VkAllocationCallbacks allocator; unsigned custom;
    struct layout_entry *next;
};
static pthread_mutex_t guard=PTHREAD_MUTEX_INITIALIZER;
static struct set_entry *sets;
static struct layout_entry *layouts;

static void free_set_info(VkDescriptorSetLayoutCreateInfo *info,const VkAllocationCallbacks *a)
{
    for(uint32_t i=0;i<info->bindingCount;++i)
        hybris_scaled_free(a,(void*)info->pBindings[i].pImmutableSamplers);
    hybris_scaled_free(a,(void*)info->pBindings);
    memset(info,0,sizeof(*info));
}
static VkResult copy_set_info(const VkDescriptorSetLayoutCreateInfo *source,
    const VkAllocationCallbacks *a,VkDescriptorSetLayoutCreateInfo *out)
{
    memset(out,0,sizeof(*out));
    if(source->pNext || source->flags || (source->bindingCount && !source->pBindings))
        return VK_ERROR_FEATURE_NOT_PRESENT;
    uint64_t bytes=(uint64_t)source->bindingCount*sizeof(VkDescriptorSetLayoutBinding);
    if(bytes>SIZE_MAX) return VK_ERROR_OUT_OF_HOST_MEMORY;
    VkDescriptorSetLayoutBinding *bindings=bytes ? hybris_scaled_alloc(a,(size_t)bytes,VK_SYSTEM_ALLOCATION_SCOPE_OBJECT) : NULL;
    if(bytes && !bindings) return VK_ERROR_OUT_OF_HOST_MEMORY;
    if(bytes) memset(bindings,0,(size_t)bytes);
    *out=*source;out->pBindings=bindings;
    for(uint32_t i=0;i<source->bindingCount;++i) {
        bindings[i]=source->pBindings[i];bindings[i].pImmutableSamplers=NULL;
        if(!source->pBindings[i].pImmutableSamplers) continue;
        bytes=(uint64_t)bindings[i].descriptorCount*sizeof(VkSampler);
        if(bytes>SIZE_MAX) {free_set_info(out,a);return VK_ERROR_OUT_OF_HOST_MEMORY;}
        VkSampler *samplers=bytes ? hybris_scaled_alloc(a,(size_t)bytes,VK_SYSTEM_ALLOCATION_SCOPE_OBJECT) : NULL;
        if(bytes && !samplers) {free_set_info(out,a);return VK_ERROR_OUT_OF_HOST_MEMORY;}
        if(bytes) memcpy(samplers,source->pBindings[i].pImmutableSamplers,(size_t)bytes);
        bindings[i].pImmutableSamplers=samplers;
    }
    return VK_SUCCESS;
}
static void free_set(struct set_entry *set)
{
    const VkAllocationCallbacks *a=set->custom ? &set->allocator : NULL;
    free_set_info(&set->info,a);hybris_scaled_free(a,set);
}
static void free_layout(struct layout_entry *l)
{
    const VkAllocationCallbacks *a=l->custom ? &l->allocator : NULL;
    for(uint32_t i=0;i<l->snapshot.info.setLayoutCount;++i) free_set_info(&l->snapshot.sets[i],a);
    hybris_scaled_free(a,l->snapshot.sets);
    hybris_scaled_free(a,(void*)l->snapshot.info.pSetLayouts);
    hybris_scaled_free(a,(void*)l->snapshot.info.pPushConstantRanges);
    hybris_scaled_free(a,l);
}
void hybris_polygon_layout_release(const struct hybris_polygon_layout *snapshot)
{
    if(!snapshot) return;
    struct layout_entry *l=(struct layout_entry*)snapshot;
    pthread_mutex_lock(&guard);int destroy=!--l->references;pthread_mutex_unlock(&guard);
    if(destroy) free_layout(l);
}
const struct hybris_polygon_layout *hybris_polygon_layout_acquire(VkDevice handle,VkPipelineLayout layout)
{
    struct hybris_layer_device device;
    if(!hybris_layer_lookup_device(handle,&device)) return NULL;
    const struct hybris_polygon_layout *out=NULL;
    pthread_mutex_lock(&guard);
    for(struct layout_entry *l=layouts;l;l=l->next)
        if(l->device==handle && l->generation==device.generation && l->handle==layout) {
            if(l->references!=SIZE_MAX) {++l->references;out=&l->snapshot;}
            break;
        }
    pthread_mutex_unlock(&guard);return out;
}
static VkResult VKAPI_CALL create_set(VkDevice handle,const VkDescriptorSetLayoutCreateInfo *info,
    const VkAllocationCallbacks *a,VkDescriptorSetLayout *out)
{
    struct hybris_layer_device device;
    if(!hybris_layer_lookup_device(handle,&device)) return VK_ERROR_INITIALIZATION_FAILED;
    struct set_entry *s=NULL;
    if(!info->pNext && !info->flags) {
        s=hybris_scaled_alloc(a,sizeof(*s),VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
        if(!s) return VK_ERROR_OUT_OF_HOST_MEMORY;
        memset(s,0,sizeof(*s));s->device=handle;s->generation=device.generation;s->custom=a!=NULL;
        if(a) s->allocator=*a;
        VkResult result=copy_set_info(info,a,&s->info);
        if(result) {free_set(s);return result;}
    }
    VkResult result=((PFN_vkCreateDescriptorSetLayout)hybris_layer_device_inner_proc(handle,"vkCreateDescriptorSetLayout"))(handle,info,a,out);
    if(result) {if(s) free_set(s);return result;}
    if(s) {s->handle=*out;pthread_mutex_lock(&guard);s->next=sets;sets=s;pthread_mutex_unlock(&guard);}
    return result;
}
static void VKAPI_CALL destroy_set(VkDevice handle,VkDescriptorSetLayout layout,const VkAllocationCallbacks *a)
{
    struct set_entry *retired=NULL;
    pthread_mutex_lock(&guard);
    for(struct set_entry **p=&sets;*p;p=&(*p)->next)
        if((*p)->device==handle && (*p)->handle==layout) {retired=*p;*p=retired->next;break;}
    pthread_mutex_unlock(&guard);
    ((PFN_vkDestroyDescriptorSetLayout)hybris_layer_device_inner_proc(handle,"vkDestroyDescriptorSetLayout"))(handle,layout,a);
    if(retired) free_set(retired);
}
static VkResult snapshot_layout(const struct hybris_layer_device *device,const VkPipelineLayoutCreateInfo *info,
    const VkAllocationCallbacks *a,struct layout_entry **out)
{
    *out=NULL;
    if(info->pNext || info->flags) return VK_ERROR_FEATURE_NOT_PRESENT;
    struct layout_entry *l=hybris_scaled_alloc(a,sizeof(*l),VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
    if(!l) return VK_ERROR_OUT_OF_HOST_MEMORY;
    memset(l,0,sizeof(*l));l->device=device->handle;l->generation=device->generation;l->references=1;
    l->custom=a!=NULL;if(a) l->allocator=*a;
    VkResult result=VK_SUCCESS;
    uint64_t bytes=(uint64_t)info->setLayoutCount*sizeof(*l->snapshot.sets);
    if(bytes>SIZE_MAX) {result=VK_ERROR_OUT_OF_HOST_MEMORY;goto fail;}
    if(bytes) {
        l->snapshot.sets=hybris_scaled_alloc(a,(size_t)bytes,VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
        if(!l->snapshot.sets) {result=VK_ERROR_OUT_OF_HOST_MEMORY;goto fail;}
        memset(l->snapshot.sets,0,(size_t)bytes);
    }
    l->snapshot.info=*info;l->snapshot.info.pSetLayouts=NULL;l->snapshot.info.pPushConstantRanges=NULL;
    pthread_mutex_lock(&guard);
    for(uint32_t i=0;i<info->setLayoutCount;++i) {
        struct set_entry *s=sets;
        while(s && !(s->device==device->handle && s->generation==device->generation && s->handle==info->pSetLayouts[i])) s=s->next;
        if(!s) {result=VK_ERROR_FEATURE_NOT_PRESENT;break;}
        result=copy_set_info(&s->info,a,&l->snapshot.sets[i]);if(result) break;
    }
    pthread_mutex_unlock(&guard);
    if(result) goto fail;
    bytes=(uint64_t)info->setLayoutCount*sizeof(VkDescriptorSetLayout);
    if(bytes>SIZE_MAX) {result=VK_ERROR_OUT_OF_HOST_MEMORY;goto fail;}
    if(bytes) {
        void *copy=hybris_scaled_alloc(a,(size_t)bytes,VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
        if(!copy) {result=VK_ERROR_OUT_OF_HOST_MEMORY;goto fail;}
        memcpy(copy,info->pSetLayouts,(size_t)bytes);l->snapshot.info.pSetLayouts=copy;
    }
    bytes=(uint64_t)info->pushConstantRangeCount*sizeof(VkPushConstantRange);
    if(bytes>SIZE_MAX) {result=VK_ERROR_OUT_OF_HOST_MEMORY;goto fail;}
    if(bytes) {
        void *copy=hybris_scaled_alloc(a,(size_t)bytes,VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
        if(!copy) {result=VK_ERROR_OUT_OF_HOST_MEMORY;goto fail;}
        memcpy(copy,info->pPushConstantRanges,(size_t)bytes);l->snapshot.info.pPushConstantRanges=copy;
    }
    *out=l;return VK_SUCCESS;
fail:
    free_layout(l);return result;
}
static VkResult VKAPI_CALL create_layout(VkDevice handle,const VkPipelineLayoutCreateInfo *info,
    const VkAllocationCallbacks *a,VkPipelineLayout *out)
{
    struct hybris_layer_device device;
    if(!hybris_layer_lookup_device(handle,&device)) return VK_ERROR_INITIALIZATION_FAILED;
    struct layout_entry *l=NULL;
    VkResult result=snapshot_layout(&device,info,a,&l);
    if(result && result!=VK_ERROR_FEATURE_NOT_PRESENT) return result;
    result=((PFN_vkCreatePipelineLayout)hybris_layer_device_inner_proc(handle,"vkCreatePipelineLayout"))(handle,info,a,out);
    if(result) {if(l) free_layout(l);return result;}
    if(l) {l->handle=*out;pthread_mutex_lock(&guard);l->next=layouts;layouts=l;pthread_mutex_unlock(&guard);}
    return result;
}
static void VKAPI_CALL destroy_layout(VkDevice handle,VkPipelineLayout layout,const VkAllocationCallbacks *a)
{
    struct layout_entry *retired=NULL;
    pthread_mutex_lock(&guard);
    for(struct layout_entry **p=&layouts;*p;p=&(*p)->next)
        if((*p)->device==handle && (*p)->handle==layout) {retired=*p;*p=retired->next;break;}
    pthread_mutex_unlock(&guard);
    ((PFN_vkDestroyPipelineLayout)hybris_layer_device_inner_proc(handle,"vkDestroyPipelineLayout"))(handle,layout,a);
    if(retired) hybris_polygon_layout_release(&retired->snapshot);
}
void hybris_polygon_layouts_release_device(VkDevice handle)
{
    for(;;) {
        pthread_mutex_lock(&guard);struct layout_entry **p=&layouts;
        while(*p && (*p)->device!=handle) p=&(*p)->next;
        struct layout_entry *l=*p;if(l) *p=l->next;pthread_mutex_unlock(&guard);
        if(!l) break;
        hybris_polygon_layout_release(&l->snapshot);
    }
    for(;;) {
        pthread_mutex_lock(&guard);struct set_entry **p=&sets;
        while(*p && (*p)->device!=handle) p=&(*p)->next;
        struct set_entry *s=*p;if(s) *p=s->next;pthread_mutex_unlock(&guard);
        if(!s) break;
        free_set(s);
    }
}
PFN_vkVoidFunction hybris_polygon_layouts_proc(const char *name)
{
    if(!strcmp(name,"vkCreateDescriptorSetLayout")) return (PFN_vkVoidFunction)create_set;
    if(!strcmp(name,"vkDestroyDescriptorSetLayout")) return (PFN_vkVoidFunction)destroy_set;
    if(!strcmp(name,"vkCreatePipelineLayout")) return (PFN_vkVoidFunction)create_layout;
    if(!strcmp(name,"vkDestroyPipelineLayout")) return (PFN_vkVoidFunction)destroy_layout;
    return NULL;
}
