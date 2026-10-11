/* SPDX-License-Identifier: Apache-2.0 */
#define VK_NO_PROTOTYPES
#include "polygon_bindings.h"
#include "vertex_resources.h"
#include <string.h>
struct slot {VkDescriptorSet source;uint32_t count;uint32_t *offsets;};
struct hybris_polygon_bindings {
    const struct hybris_polygon_pipeline *pipeline;
    const struct hybris_bc_layout *identity;
    const struct hybris_bc_command_state *journal;
    VkDescriptorPool pool;
    VkDescriptorSet *sets;
    struct slot *slots;
    VkDevice device;
    PFN_vkGetDeviceProcAddr resolver;
    uint32_t set_count;
    VkAllocationCallbacks allocator;unsigned custom;
};
static const VkAllocationCallbacks *allocator(const struct hybris_polygon_bindings *b)
{return b->custom ? &b->allocator : NULL;}
void hybris_polygon_bindings_destroy(struct hybris_polygon_bindings *b)
{
    if(!b) return;
    if(b->pool) ((PFN_vkDestroyDescriptorPool)b->resolver(b->device,"vkDestroyDescriptorPool"))(b->device,b->pool,allocator(b));
    if(b->slots) for(uint32_t i=0;i<b->set_count;++i) hybris_scaled_free(allocator(b),b->slots[i].offsets);
    hybris_scaled_free(allocator(b),b->slots);hybris_scaled_free(allocator(b),b->sets);
    hybris_scaled_free(allocator(b),b);
}
static VkResult collect(void *user,const struct hybris_bc_layout *identity,uint32_t first,uint32_t count,
    const VkDescriptorSet *sets,uint32_t offset_count,const uint32_t *offsets)
{
    struct hybris_polygon_bindings *b=user;
    if(identity!=b->identity || first>b->pipeline->reserved_set || count>b->pipeline->reserved_set-first)
        return VK_ERROR_FEATURE_NOT_PRESENT;
    uint64_t total=0;
    for(uint32_t i=0;i<count;++i) total+=b->slots[first+i].count;
    if(total!=offset_count) return VK_ERROR_FEATURE_NOT_PRESENT;
    uint32_t cursor=0;
    for(uint32_t i=0;i<count;++i) {
        if(!sets[i]) return VK_ERROR_FEATURE_NOT_PRESENT;
        struct slot *slot=&b->slots[first+i];slot->source=sets[i];
        if(slot->count) memcpy(slot->offsets,offsets+cursor,(size_t)slot->count*sizeof(uint32_t));
        cursor+=slot->count;
    }
    return VK_SUCCESS;
}
static VkResult validate_push(void *user,const struct hybris_bc_layout *identity,
    VkShaderStageFlags stages,uint32_t offset,uint32_t size,const void *values)
{
    (void)stages;(void)offset;(void)size;(void)values;
    const struct hybris_polygon_bindings *b=user;
    return identity==b->identity ? VK_SUCCESS : VK_ERROR_FEATURE_NOT_PRESENT;
}
VkResult hybris_polygon_bindings_prepare(const struct hybris_polygon_pipeline *p,
    const VkDescriptorSetLayoutCreateInfo *definitions,const struct hybris_bc_layout *identity,
    const struct hybris_bc_command_state *journal,const VkAllocationCallbacks *a,
    struct hybris_polygon_bindings **out)
{
    *out=NULL;
    if(!p || !identity || !journal || (p->reserved_set && !definitions)) return VK_ERROR_FEATURE_NOT_PRESENT;
    struct hybris_polygon_bindings *b=hybris_scaled_alloc(a,sizeof(*b),VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
    if(!b) return VK_ERROR_OUT_OF_HOST_MEMORY;
    memset(b,0,sizeof(*b));b->pipeline=p;b->identity=identity;b->journal=journal;b->custom=a!=NULL;
    b->device=p->device;b->resolver=p->resolver;b->set_count=p->reserved_set;
    if(a) b->allocator=*a;
    VkResult result=VK_SUCCESS;
    uint64_t bytes=(uint64_t)p->reserved_set*sizeof(*b->slots);
    if(bytes>SIZE_MAX) {result=VK_ERROR_OUT_OF_HOST_MEMORY;goto fail;}
    if(bytes) {
        b->slots=hybris_scaled_alloc(a,(size_t)bytes,VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
        if(!b->slots) {result=VK_ERROR_OUT_OF_HOST_MEMORY;goto fail;}
        memset(b->slots,0,(size_t)bytes);
    }
    VkDescriptorPoolSize sizes[VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT+1];uint32_t size_count=0;
    memset(sizes,0,sizeof(sizes));
    for(uint32_t i=0;i<p->reserved_set;++i) {
        const VkDescriptorSetLayoutCreateInfo *info=&definitions[i];
        if(info->pNext || info->flags) {result=VK_ERROR_FEATURE_NOT_PRESENT;goto fail;}
        uint64_t offsets=0;
        for(uint32_t j=0;j<info->bindingCount;++j) {
            const VkDescriptorSetLayoutBinding *binding=&info->pBindings[j];
            if(binding->descriptorType>VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT) {result=VK_ERROR_FEATURE_NOT_PRESENT;goto fail;}
            VkDescriptorPoolSize *size=&sizes[binding->descriptorType];size->type=binding->descriptorType;
            if(binding->descriptorCount>UINT32_MAX-size->descriptorCount) {result=VK_ERROR_FEATURE_NOT_PRESENT;goto fail;}
            size->descriptorCount+=binding->descriptorCount;
            if(binding->descriptorType==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
                binding->descriptorType==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC) offsets+=binding->descriptorCount;
        }
        if(offsets>UINT32_MAX || offsets>SIZE_MAX/sizeof(uint32_t)) {result=VK_ERROR_FEATURE_NOT_PRESENT;goto fail;}
        b->slots[i].count=(uint32_t)offsets;
        if(offsets) {
            b->slots[i].offsets=hybris_scaled_alloc(a,(size_t)offsets*sizeof(uint32_t),VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
            if(!b->slots[i].offsets) {result=VK_ERROR_OUT_OF_HOST_MEMORY;goto fail;}
        }
    }
    struct hybris_bc_graphics_visitor visitor={b,collect,validate_push};
    result=hybris_bc_visit_graphics(journal,&visitor);if(result) goto fail;
    for(uint32_t i=0;i<p->reserved_set;++i)
        if(definitions[i].bindingCount && !b->slots[i].source) {result=VK_ERROR_FEATURE_NOT_PRESENT;goto fail;}
    if(!p->reserved_set) {*out=b;return VK_SUCCESS;}
    for(unsigned i=0;i<=VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;++i)
        if(sizes[i].descriptorCount) sizes[size_count++]=sizes[i];
    VkDescriptorPoolCreateInfo pool={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets=p->reserved_set,.poolSizeCount=size_count,.pPoolSizes=sizes};
    result=((PFN_vkCreateDescriptorPool)p->resolver(p->device,"vkCreateDescriptorPool"))(p->device,&pool,a,&b->pool);
    if(result) goto fail;
    bytes=(uint64_t)p->reserved_set*sizeof(*b->sets);
    if(bytes>SIZE_MAX) {result=VK_ERROR_OUT_OF_HOST_MEMORY;goto fail;}
    b->sets=hybris_scaled_alloc(a,(size_t)bytes,VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
    if(!b->sets) {result=VK_ERROR_OUT_OF_HOST_MEMORY;goto fail;}
    VkDescriptorSetAllocateInfo allocate={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool=b->pool,.descriptorSetCount=p->reserved_set,.pSetLayouts=p->sets};
    result=((PFN_vkAllocateDescriptorSets)p->resolver(p->device,"vkAllocateDescriptorSets"))(p->device,&allocate,b->sets);
    if(result) goto fail;
    PFN_vkUpdateDescriptorSets update=(void*)p->resolver(p->device,"vkUpdateDescriptorSets");
    for(uint32_t i=0;i<p->reserved_set;++i) {
        const VkDescriptorSetLayoutCreateInfo *info=&definitions[i];
        bytes=(uint64_t)info->bindingCount*sizeof(uint32_t);
        if(bytes>SIZE_MAX) {result=VK_ERROR_OUT_OF_HOST_MEMORY;goto fail;}
        uint32_t *counts=bytes ? hybris_scaled_alloc(a,(size_t)bytes,VK_SYSTEM_ALLOCATION_SCOPE_COMMAND) : NULL;
        if(bytes && !counts) {result=VK_ERROR_OUT_OF_HOST_MEMORY;goto fail;}
        for(uint32_t j=0;j<info->bindingCount;++j) counts[j]=info->pBindings[j].descriptorCount;
        result=hybris_vertex_resource_copy(p->device,update,b->slots[i].source,b->sets[i],
            info->bindingCount,info->pBindings,counts,a);
        hybris_scaled_free(a,counts);if(result) goto fail;
    }
    *out=b;return VK_SUCCESS;
fail:
    hybris_polygon_bindings_destroy(b);return result;
}
struct push_target {const struct hybris_polygon_bindings *bindings;VkCommandBuffer command;};
static VkResult replay_push(void *user,const struct hybris_bc_layout *identity,
    VkShaderStageFlags stages,uint32_t offset,uint32_t size,const void *values)
{
    struct push_target *target=user;
    const struct hybris_polygon_pipeline *p=target->bindings->pipeline;
    if(identity!=target->bindings->identity) return VK_ERROR_FEATURE_NOT_PRESENT;
    ((PFN_vkCmdPushConstants)p->resolver(p->device,"vkCmdPushConstants"))(target->command,p->layout,
        stages|VK_SHADER_STAGE_COMPUTE_BIT,offset,size,values);
    return VK_SUCCESS;
}
VkResult hybris_polygon_bindings_bind(const struct hybris_polygon_bindings *b,
    VkCommandBuffer command,VkPipelineBindPoint point)
{
    if(!b || !command || (point!=VK_PIPELINE_BIND_POINT_COMPUTE && point!=VK_PIPELINE_BIND_POINT_GRAPHICS))
        return VK_ERROR_INITIALIZATION_FAILED;
    struct hybris_bc_graphics_visitor validate={(void*)b,NULL,validate_push};
    VkResult result=hybris_bc_visit_graphics(b->journal,&validate);if(result) return result;
    const struct hybris_polygon_pipeline *p=b->pipeline;
    PFN_vkCmdBindDescriptorSets bind=(void*)p->resolver(p->device,"vkCmdBindDescriptorSets");
    for(uint32_t i=0;i<p->reserved_set;++i)
        bind(command,point,p->layout,i,1,&b->sets[i],b->slots[i].count,b->slots[i].offsets);
    struct push_target target={b,command};
    struct hybris_bc_graphics_visitor visitor={&target,NULL,replay_push};
    return hybris_bc_visit_graphics(b->journal,&visitor);
}
