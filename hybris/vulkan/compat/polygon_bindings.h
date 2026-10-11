/* SPDX-License-Identifier: Apache-2.0 */
#ifndef HYBRIS_POLYGON_BINDINGS_H
#define HYBRIS_POLYGON_BINDINGS_H
#include "polygon_pipeline.h"
#include "bc_command_state.h"
struct hybris_polygon_bindings;
/* Resolve the journal's latest graphics bindings, copy live descriptors into
 * compute-visible companion sets, and preserve per-set dynamic offsets.
 * No GPU commands are emitted during prepare. Mixed layout lifetimes and
 * unsupported declarations return FEATURE_NOT_PRESENT for whole filled draw.
 * Application definitions must match pipeline; identity is retained separately
 * by its owner. The journal must stay unchanged until bind completes. */
VkResult hybris_polygon_bindings_prepare(const struct hybris_polygon_pipeline *pipeline,
    const VkDescriptorSetLayoutCreateInfo *application_sets,
    const struct hybris_bc_layout *identity, const struct hybris_bc_command_state *journal,
    const VkAllocationCallbacks *allocator, struct hybris_polygon_bindings **out);
/* Bind companion resources/push data for vertex replay and compute producer.
 * Caller binds the scratch set and pipeline, then restores its journal after
 * internal computation. All referenced command buffers must retire before
 * destroy: descriptor pools are not temporary recording-time resources. */
VkResult hybris_polygon_bindings_bind(const struct hybris_polygon_bindings *bindings,
    VkCommandBuffer command, VkPipelineBindPoint point);
void hybris_polygon_bindings_destroy(struct hybris_polygon_bindings *bindings);
#endif
