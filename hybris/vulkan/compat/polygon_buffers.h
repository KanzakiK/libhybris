/* SPDX-License-Identifier: Apache-2.0 */
#ifndef HYBRIS_POLYGON_BUFFERS_H
#define HYBRIS_POLYGON_BUFFERS_H
#include "polygon_bindings.h"
#include "polygon_indices.h"
struct hybris_polygon_draw {
    VkBool32 indexed;
    uint32_t count, instances, first, first_instance;
    int32_t vertex_offset;
    VkIndexType index_type;
    VkPolygonMode mode;
    VkCullModeFlags cull_mode;
    VkFrontFace front_face;
    VkBool32 inverted_viewport;
    uint32_t index_capacity;
};
struct hybris_polygon_buffers {
    VkDevice device;
    PFN_vkGetDeviceProcAddr resolver;
    VkAllocationCallbacks allocator;
    unsigned custom_allocator;
    /* Parameters, post-VS records, ordinal indices, generated indices,
     * converted + original indirect commands. Only metadata is host mapped. */
    VkBuffer buffers[5];
    VkDeviceMemory memory[5];
    VkDeviceSize sizes[5];
    VkDescriptorPool pool;
    VkDescriptorSet producer_set, indices_set;
    uint32_t invocations;
    struct hybris_polygon_push push;
};
/* Prepare before emitting GPU commands. Raw input descriptors must already
 * have been checked against live STORAGE_BUFFER allocations. Unsupported
 * bounds/limits return FEATURE_NOT_PRESENT: issue the complete filled draw.
 * Indexed vertex addresses follow the application's valid index contract;
 * indices are never read back to the CPU to validate/expand them. */
VkResult hybris_polygon_buffers_create(const struct hybris_polygon_pipeline *pipeline,
    const struct hybris_polygon_indices *converter,
    const VkPhysicalDeviceMemoryProperties *memory, const VkPhysicalDeviceLimits *limits,
    const struct hybris_polygon_draw *draw, const VkDescriptorBufferInfo *attributes,
    const VkDescriptorBufferInfo *indices, const VkAllocationCallbacks *allocator,
    struct hybris_polygon_buffers **out);
/* Outside rendering, after application writes are visible to compute reads.
 * Establishes producer -> assembly -> raster dependencies. Caller resumes
 * rendering, invokes polygon_raster_record, then restores its compute state. */
VkResult hybris_polygon_buffers_compute(const struct hybris_polygon_buffers *buffers,
    const struct hybris_polygon_pipeline *pipeline, const struct hybris_polygon_indices *converter,
    const struct hybris_polygon_bindings *bindings, VkCommandBuffer command);
/* Suitable for command_retain; destroy only when recorded commands retire. */
void hybris_polygon_buffers_destroy(void *buffers);
#endif
