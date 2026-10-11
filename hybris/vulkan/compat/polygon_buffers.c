/* SPDX-License-Identifier: Apache-2.0 */
#define VK_NO_PROTOTYPES
#include "polygon_buffers.h"
#include <string.h>
#define PROC(n) PFN_vk##n n=(PFN_vk##n)b->resolver(b->device,"vk" #n)
void hybris_polygon_buffers_destroy(void *object)
{
    struct hybris_polygon_buffers *b=object;
    if(!b) return;
    const VkAllocationCallbacks *a=b->custom_allocator ? &b->allocator : NULL;
    PROC(DestroyDescriptorPool);PROC(DestroyBuffer);PROC(FreeMemory);
    if(b->pool) DestroyDescriptorPool(b->device,b->pool,a);
    for(unsigned i=0;i<5;++i) {
        if(b->buffers[i]) DestroyBuffer(b->device,b->buffers[i],a);
        if(b->memory[i]) FreeMemory(b->device,b->memory[i],a);
    }
    hybris_scaled_free(a,b);
}
static int raw_range(const VkDescriptorBufferInfo *d,const VkPhysicalDeviceLimits *l)
{
    return d && d->buffer && d->range && d->range!=VK_WHOLE_SIZE &&
        !(d->range%4) && d->range<=l->maxStorageBufferRange &&
        (!l->minStorageBufferOffsetAlignment || !(d->offset%l->minStorageBufferOffsetAlignment));
}
static VkResult allocate_buffer(struct hybris_polygon_buffers *b,unsigned i,
    const VkPhysicalDeviceMemoryProperties *m,VkBufferUsageFlags usage,VkBool32 host)
{
    const VkAllocationCallbacks *a=b->custom_allocator ? &b->allocator : NULL;
    PROC(CreateBuffer);PROC(GetBufferMemoryRequirements);PROC(AllocateMemory);PROC(BindBufferMemory);
    VkBufferCreateInfo info={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=b->sizes[i],
        .usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT|usage,
        .sharingMode=VK_SHARING_MODE_EXCLUSIVE};
    VkResult r=CreateBuffer(b->device,&info,a,&b->buffers[i]);if(r) return r;
    VkMemoryRequirements req;GetBufferMemoryRequirements(b->device,b->buffers[i],&req);
    VkMemoryPropertyFlags flags=host ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT :
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    uint32_t type=m->memoryTypeCount;
    for(uint32_t t=0;t<m->memoryTypeCount;++t)
        if((req.memoryTypeBits&(1u<<t)) && (m->memoryTypes[t].propertyFlags&flags)==flags) {type=t;break;}
    if(type==m->memoryTypeCount) return VK_ERROR_FEATURE_NOT_PRESENT;
    VkMemoryAllocateInfo alloc={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=type};
    r=AllocateMemory(b->device,&alloc,a,&b->memory[i]);if(r) return r;
    return BindBufferMemory(b->device,b->buffers[i],b->memory[i],0);
}
VkResult hybris_polygon_buffers_create(const struct hybris_polygon_pipeline *p,
    const struct hybris_polygon_indices *k,const VkPhysicalDeviceMemoryProperties *memory,
    const VkPhysicalDeviceLimits *l,const struct hybris_polygon_draw *d,
    const VkDescriptorBufferInfo *attributes,const VkDescriptorBufferInfo *index,
    const VkAllocationCallbacks *allocator,struct hybris_polygon_buffers **out)
{
    if(!out) return VK_ERROR_INITIALIZATION_FAILED;
    *out=NULL;
    if(!p || !k || !memory || !l || !d || p->device!=k->device || !p->resolver ||
        !p->sets || !k->descriptors || memory->memoryTypeCount>VK_MAX_MEMORY_TYPES ||
        !d->count || !d->instances || d->count%3 || p->records.input_count>64 ||
        !p->records.output_stride || p->records.output_stride%16 ||
        (p->records.input_count && !attributes) ||
        (d->mode!=VK_POLYGON_MODE_LINE && d->mode!=VK_POLYGON_MODE_POINT) ||
        (d->cull_mode&~VK_CULL_MODE_FRONT_AND_BACK) || d->front_face>VK_FRONT_FACE_CLOCKWISE ||
        (uint64_t)d->first_instance+d->instances-1>UINT32_MAX)
        return VK_ERROR_FEATURE_NOT_PRESENT;
    uint64_t n=(uint64_t)d->count*d->instances;
    if(n>UINT32_MAX || n>l->maxComputeWorkGroupCount[0] ||
        !l->maxComputeWorkGroupSize[0] || !l->maxComputeWorkGroupInvocations)
        return VK_ERROR_FEATURE_NOT_PRESENT;
    uint32_t width=0;
    if(d->indexed) {
        if(d->index_type==VK_INDEX_TYPE_UINT16) width=2;
        else if(d->index_type==VK_INDEX_TYPE_UINT32) width=4;
        else return VK_ERROR_FEATURE_NOT_PRESENT;
        if(!raw_range(index,l) || ((uint64_t)d->first+d->count)*width>index->range)
            return VK_ERROR_FEATURE_NOT_PRESENT;
    } else if((uint64_t)d->first+d->count-1>UINT32_MAX) return VK_ERROR_FEATURE_NOT_PRESENT;
    for(uint32_t i=0;i<p->records.input_count;++i) {
        if(!raw_range(&attributes[i],l)) return VK_ERROR_FEATURE_NOT_PRESENT;
        const VkVertexInputBindingDescription *binding=&p->bindings[i];
        if(binding->inputRate!=VK_VERTEX_INPUT_RATE_VERTEX && binding->inputRate!=VK_VERTEX_INPUT_RATE_INSTANCE)
            return VK_ERROR_FEATURE_NOT_PRESENT;
        if(!d->indexed || binding->inputRate==VK_VERTEX_INPUT_RATE_INSTANCE) {
            uint64_t last=binding->inputRate==VK_VERTEX_INPUT_RATE_INSTANCE ?
                (uint64_t)d->first_instance+d->instances-1 : (uint64_t)d->first+d->count-1;
            uint64_t end=last*binding->stride+p->attributes[i].offset+
                4u*(p->records.inputs[i].component+p->records.inputs[i].components);
            if(end>attributes[i].range || end>UINT32_MAX) return VK_ERROR_FEATURE_NOT_PRESENT;
        }
    }
    VkDeviceSize sizes[]={24,n*p->records.output_stride,n*4,(VkDeviceSize)d->index_capacity*4,40};
    if(!sizes[3]) sizes[3]=4;
    for(unsigned i=0;i<5;++i) if(sizes[i]>l->maxStorageBufferRange) return VK_ERROR_FEATURE_NOT_PRESENT;
    struct hybris_polygon_buffers *b=hybris_scaled_alloc(allocator,sizeof(*b),VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
    if(!b) return VK_ERROR_OUT_OF_HOST_MEMORY;
    memset(b,0,sizeof(*b));b->device=p->device;b->resolver=p->resolver;
    b->custom_allocator=allocator!=NULL;
    if(allocator) b->allocator=*allocator;
    b->invocations=(uint32_t)n;memcpy(b->sizes,sizes,sizeof(sizes));
    b->push=(struct hybris_polygon_push){.triangle_count=(uint32_t)n/3,.mode=d->mode,
        .cull_mode=d->cull_mode,.front_ccw=d->front_face==VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .inverted_viewport=d->inverted_viewport,.record_count=(uint32_t)n,
        .capacity=d->index_capacity,.record_stride=p->records.output_stride/16,
        .filled={d->count,d->instances,d->first,d->indexed ? d->vertex_offset : (int32_t)d->first_instance,
            d->indexed ? d->first_instance : 0}};
    VkResult r=VK_SUCCESS;
    for(unsigned i=0;i<5;++i) {
        r=allocate_buffer(b,i,memory,i==3 ? VK_BUFFER_USAGE_INDEX_BUFFER_BIT :
            (i==4 ? VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT : 0),i==0 || i==2);
        if(r) goto fail;
    }
    PROC(MapMemory);PROC(UnmapMemory);
    void *mapped=NULL;
    r=MapMemory(b->device,b->memory[0],0,24,0,&mapped);if(r) goto fail;
    uint32_t params[]={d->indexed ? 0 : d->first,d->indexed ? d->first : 0,
        d->indexed ? (uint32_t)d->vertex_offset : 0,d->first_instance,d->count,width};
    memcpy(mapped,params,sizeof(params));UnmapMemory(b->device,b->memory[0]);
    r=MapMemory(b->device,b->memory[2],0,b->sizes[2],0,&mapped);if(r) goto fail;
    for(uint32_t i=0;i<b->invocations;++i) ((uint32_t*)mapped)[i]=i;
    UnmapMemory(b->device,b->memory[2]);
    PROC(CreateDescriptorPool);PROC(AllocateDescriptorSets);PROC(UpdateDescriptorSets);
    VkDescriptorPoolSize pool_size={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,p->records.input_count+7};
    VkDescriptorPoolCreateInfo pool={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets=2,.poolSizeCount=1,.pPoolSizes=&pool_size};
    r=CreateDescriptorPool(b->device,&pool,allocator,&b->pool);if(r) goto fail;
    VkDescriptorSetLayout layouts[]={p->sets[p->reserved_set],k->descriptors};
    VkDescriptorSetAllocateInfo sets={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool=b->pool,.descriptorSetCount=2,.pSetLayouts=layouts};
    VkDescriptorSet handles[2];r=AllocateDescriptorSets(b->device,&sets,handles);if(r) goto fail;
    b->producer_set=handles[0];b->indices_set=handles[1];
    VkDescriptorBufferInfo infos[71];VkWriteDescriptorSet writes[71];uint32_t total=p->records.input_count+7;
    for(uint32_t i=0;i<total;++i) {
        uint32_t producer_count=p->records.input_count+3;
        if(i<producer_count) {
            if(i<2) infos[i]=(VkDescriptorBufferInfo){b->buffers[i],0,b->sizes[i]};
            else if(i<producer_count-1) infos[i]=attributes[i-2];
            else infos[i]=d->indexed ? *index : (VkDescriptorBufferInfo){b->buffers[0],0,b->sizes[0]};
        } else {uint32_t slot=i-producer_count+1;infos[i]=(VkDescriptorBufferInfo){b->buffers[slot],0,b->sizes[slot]};}
        writes[i]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet=i<producer_count ? b->producer_set : b->indices_set,
            .dstBinding=i<producer_count ? i : i-producer_count,.descriptorCount=1,
            .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&infos[i]};
    }
    UpdateDescriptorSets(b->device,total,writes,0,NULL);*out=b;return VK_SUCCESS;
fail:
    hybris_polygon_buffers_destroy(b);return r;
}
VkResult hybris_polygon_buffers_compute(const struct hybris_polygon_buffers *b,
    const struct hybris_polygon_pipeline *p,const struct hybris_polygon_indices *k,
    const struct hybris_polygon_bindings *bindings,VkCommandBuffer command)
{
    if(!b || !p || !k || !bindings || !command || b->device!=p->device || b->device!=k->device)
        return VK_ERROR_INITIALIZATION_FAILED;
    PROC(CmdBindPipeline);PROC(CmdBindDescriptorSets);PROC(CmdDispatch);PROC(CmdPipelineBarrier);
    VkResult r=hybris_polygon_bindings_bind(bindings,command,VK_PIPELINE_BIND_POINT_COMPUTE);if(r) return r;
    CmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,p->producer);
    CmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,p->layout,p->reserved_set,1,&b->producer_set,0,NULL);
    CmdDispatch(command,b->invocations,1,1);
    VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_SHADER_READ_BIT};
    CmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,1,&barrier,0,NULL,0,NULL);
    r=hybris_polygon_indices_record(k,command,b->indices_set,&b->push);if(r) return r;
    barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_INDEX_READ_BIT|VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    CmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_VERTEX_SHADER_BIT|VK_PIPELINE_STAGE_VERTEX_INPUT_BIT|VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
        0,1,&barrier,0,NULL,0,NULL);
    return VK_SUCCESS;
}
