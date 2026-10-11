/* SPDX-License-Identifier: Apache-2.0 */
#define VK_NO_PROTOTYPES
#include "vertex_resources.h"
#include "scaled_vertex.h"

VkResult hybris_vertex_resource_layout(VkDevice device,
    PFN_vkCreateDescriptorSetLayout create, const VkDescriptorSetLayoutCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkDescriptorSetLayout *output)
{
    *output=VK_NULL_HANDLE;
    if(info->pNext || info->flags || (info->bindingCount && !info->pBindings))
        return VK_ERROR_FEATURE_NOT_PRESENT;
    uint64_t bytes=(uint64_t)info->bindingCount*sizeof(*info->pBindings);
    if(bytes>SIZE_MAX) return VK_ERROR_OUT_OF_HOST_MEMORY;
    VkDescriptorSetLayoutBinding *bindings=bytes ? hybris_scaled_alloc(allocator,(size_t)bytes,
        VK_SYSTEM_ALLOCATION_SCOPE_COMMAND) : NULL;
    if(bytes && !bindings) return VK_ERROR_OUT_OF_HOST_MEMORY;
    for(uint32_t i=0;i<info->bindingCount;++i) {
        bindings[i]=info->pBindings[i];
        /* Retain visibility for replay stages and pipeline layout compatibility
         * within the companion chain. Original layouts/sets are untouched. */
        if(bindings[i].stageFlags & VK_SHADER_STAGE_VERTEX_BIT)
            bindings[i].stageFlags|=VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo clone=*info;clone.pBindings=bindings;
    VkResult result=create(device,&clone,allocator,output);
    hybris_scaled_free(allocator,bindings);
    return result;
}

VkResult hybris_vertex_resource_copy(VkDevice device, PFN_vkUpdateDescriptorSets update,
    VkDescriptorSet source, VkDescriptorSet destination, uint32_t binding_count,
    const VkDescriptorSetLayoutBinding *bindings, const uint32_t *binding_counts,
    const VkAllocationCallbacks *allocator)
{
    if(binding_count && (!bindings || !binding_counts)) return VK_ERROR_FEATURE_NOT_PRESENT;
    uint64_t bytes=(uint64_t)binding_count*sizeof(VkCopyDescriptorSet);
    if(bytes>SIZE_MAX) return VK_ERROR_OUT_OF_HOST_MEMORY;
    VkCopyDescriptorSet *copies=bytes ? hybris_scaled_alloc(allocator,(size_t)bytes,
        VK_SYSTEM_ALLOCATION_SCOPE_COMMAND) : NULL;
    if(bytes && !copies) return VK_ERROR_OUT_OF_HOST_MEMORY;
    uint32_t count=0;
    for(uint32_t i=0;i<binding_count;++i) {
        if(binding_counts[i]>bindings[i].descriptorCount) {
            hybris_scaled_free(allocator,copies);return VK_ERROR_FEATURE_NOT_PRESENT;
        }
        if(!binding_counts[i]) continue;
        copies[count++]=(VkCopyDescriptorSet){.sType=VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET,
            .srcSet=source,.srcBinding=bindings[i].binding,.dstSet=destination,
            .dstBinding=bindings[i].binding,.descriptorCount=binding_counts[i]};
    }
    if(count) update(device,0,NULL,count,copies);
    hybris_scaled_free(allocator,copies);
    return VK_SUCCESS;
}
