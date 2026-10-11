/* SPDX-License-Identifier: Apache-2.0 */
#ifndef HYBRIS_POLYGON_INDICES_H
#define HYBRIS_POLYGON_INDICES_H
#include <vulkan/vulkan.h>
struct hybris_polygon_indices {
    VkDevice device;
    PFN_vkGetDeviceProcAddr resolver;
    VkDescriptorSetLayout descriptors;
    VkPipelineLayout layout;
    VkPipeline pipeline;
};
struct hybris_polygon_push {
    uint32_t triangle_count, mode, cull_mode, front_ccw;
    uint32_t inverted_viewport, record_count, capacity, record_stride;
    /* Exact original arguments. For a non-indexed fallback, store the four
     * VkDrawIndirectCommand words here and zero the fifth word. */
    VkDrawIndexedIndirectCommand filled;
};
/* Own one conversion pipeline per device. The caller supplies the same
 * allocator to create/destroy and retires recorded commands before destroy.
 * Descriptor bindings: post-VS records, ordinal triangle indices, generated
 * indices, two 20-byte indirect slots (second may hold VkDrawIndirectCommand).
 * All buffers stay on GPU. */
VkResult hybris_polygon_indices_create(VkDevice device, PFN_vkGetDeviceProcAddr resolver,
    const VkAllocationCallbacks *allocator, struct hybris_polygon_indices *out);
void hybris_polygon_indices_destroy(struct hybris_polygon_indices *indices,
    const VkAllocationCallbacks *allocator);
/* Outside a rendering scope. No state restoration here: the owning command
 * adapter restores its existing journal after all internal compute work.
 * Unsupported/overcapacity geometry selects the entire filled command on GPU.
 * Caller validates descriptor ranges and keeps descriptors/buffers alive. */
VkResult hybris_polygon_indices_record(const struct hybris_polygon_indices *indices,
    VkCommandBuffer command, VkDescriptorSet descriptors, const struct hybris_polygon_push *push);
#endif
