/* SPDX-License-Identifier: Apache-2.0 */
#ifndef HYBRIS_POLYGON_PIPELINE_H
#define HYBRIS_POLYGON_PIPELINE_H
#include "vertex_compute.h"
#include "bc_command_state.h"
#include "polygon_layouts.h"
struct hybris_polygon_pipeline {
    VkDevice device;
    PFN_vkGetDeviceProcAddr resolver;
    VkAllocationCallbacks allocator;
    unsigned custom_allocator;
    VkPipeline producer, lines, points, filled;
    VkPipelineLayout layout;
    VkDescriptorSetLayout *sets;
    uint32_t reserved_set;
    struct hybris_vertex_compute_layout records;
    VkVertexInputAttributeDescription attributes[64];
    VkVertexInputBindingDescription bindings[64];
    /* Application-visible handle is filled. Command references retain this
     * exact lifetime even after the handle is destroyed or reused. */
    VkPolygonMode mode;
    VkCullModeFlags cull_mode;
    VkFrontFace front_face;
    VkViewport viewport;
    VkBool32 dynamic_viewport;
    const struct hybris_polygon_layout *application_layout;
    struct hybris_bc_layouts *journal_layouts;
    struct hybris_bc_layout *identity;
    unsigned references;
    struct hybris_polygon_pipeline *next;
};
/* Input layout/set declarations must still be live during this call. Returned
 * companion layouts/pipelines are owned by the object, not the application.
 * The original filled pipeline retains original shaders/input/descriptor
 * layouts. Unsupported conversion returns FEATURE_NOT_PRESENT with *out=NULL;
 * the command adapter must then issue the complete original filled draw.
 * No capability is advertised and no application draw is intercepted here. */
VkResult hybris_polygon_pipeline_create(VkDevice device, PFN_vkGetDeviceProcAddr resolver,
    const VkGraphicsPipelineCreateInfo *graphics,
    const VkPipelineLayoutCreateInfo *application_layout,
    const VkDescriptorSetLayoutCreateInfo *application_sets,
    const uint32_t *vertex_code, size_t vertex_size,
    const VkAllocationCallbacks *allocator, struct hybris_polygon_pipeline **out);
/* All commands referencing these pipelines must have retired, as for the
 * application's original Vulkan pipeline. Partial construction is safe. */
void hybris_polygon_pipeline_destroy(struct hybris_polygon_pipeline *pipeline);
/* Publish transfers the pipeline and the two acquired declaration references
 * on success only. Native handles must not already have a live owner.
 * Remove intercepts application DestroyPipeline; unowned handles are forwarded
 * normally. Command references are released after command retirement. */
VkResult hybris_polygon_pipeline_publish(struct hybris_polygon_pipeline *pipeline,
    const struct hybris_polygon_layout *layout,struct hybris_bc_layouts *journal_layouts,
    struct hybris_bc_layout *identity);
struct hybris_polygon_pipeline *hybris_polygon_pipeline_acquire(VkDevice device,VkPipeline handle);
void hybris_polygon_pipeline_release(void *pipeline);
int hybris_polygon_pipeline_remove(VkDevice device,VkPipeline handle);
void hybris_polygon_pipelines_release_device(VkDevice device);
#endif
