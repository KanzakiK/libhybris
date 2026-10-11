/* SPDX-License-Identifier: Apache-2.0 */
#ifndef HYBRIS_VERTEX_RESOURCES_H
#define HYBRIS_VERTEX_RESOURCES_H
#include <vulkan/vulkan.h>
/* Create a compute-visible companion without changing the application's set
 * layout. The application sets remain usable by its original graphics draws.
 * Unsupported extension/flag combinations select the whole filled path. */
VkResult hybris_vertex_resource_layout(VkDevice device,
    PFN_vkCreateDescriptorSetLayout create, const VkDescriptorSetLayoutCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkDescriptorSetLayout *output);
/* Copy descriptors through Vulkan, not by reading buffers or shadowing every
 * application update. Both sets must be live. binding_counts holds allocation
 * counts (not layout maxima); dynamic offsets are passed unchanged at bind.
 * Caller owns the companion set/pool until the recorded commands retire. */
VkResult hybris_vertex_resource_copy(VkDevice device, PFN_vkUpdateDescriptorSets update,
    VkDescriptorSet source, VkDescriptorSet destination, uint32_t binding_count,
    const VkDescriptorSetLayoutBinding *bindings, const uint32_t *binding_counts,
    const VkAllocationCallbacks *allocator);
#endif
