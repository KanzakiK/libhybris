/* SPDX-License-Identifier: Apache-2.0 */
#ifndef HYBRIS_POLYGON_RASTER_H
#define HYBRIS_POLYGON_RASTER_H
#include "polygon_bindings.h"
struct hybris_polygon_source {
    uint32_t first_binding, binding_count;
    const VkBuffer *vertices;
    const VkDeviceSize *offsets;
    VkBool32 indexed;
    VkBuffer indices;
    VkDeviceSize index_offset;
    VkIndexType index_type;
};
/* Inside a supported rendering scope, after compute -> index/indirect/vertex
 * dependencies. GPU-written counts select exactly one of converted/original
 * draws; no CPU decision/readback. Journal graphics pipeline is the original
 * filled pipeline, with its original sets/push data. Its vertex/index bindings
 * are restored from source; indirect's second record contains exact original
 * arguments (VkDrawIndirectCommand packing for non-indexed draws).
 * Nonzero firstInstance requires native drawIndirectFirstInstance enabled. */
VkResult hybris_polygon_raster_record(const struct hybris_polygon_pipeline *pipeline,
    const struct hybris_polygon_bindings *bindings, struct hybris_bc_command_state *journal,
    VkCommandBuffer command, VkPolygonMode mode, VkDescriptorSet scratch,
    VkBuffer generated_indices, VkBuffer indirect, const struct hybris_polygon_source *source);
#endif
