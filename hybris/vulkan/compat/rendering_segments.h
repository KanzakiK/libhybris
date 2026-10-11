/* SPDX-License-Identifier: Apache-2.0 */
#ifndef HYBRIS_RENDERING_SEGMENTS_H
#define HYBRIS_RENDERING_SEGMENTS_H
#include <vulkan/vulkan.h>
/* An explicitly interruptible dynamic-rendering scope. Zero-initialize it,
 * and reset on command-buffer retirement. Unknown extension chains, secondary
 * contents and legacy render passes must use the original filled path instead.
 * The caller must not pause while a rendering query is active. */
struct hybris_rendering_scope {
    VkRenderingInfo info;
    VkRenderingAttachmentInfo *colors, depth, stencil;
    VkAllocationCallbacks allocator;
    unsigned custom_allocator, active, paused;
};
void hybris_rendering_scope_reset(struct hybris_rendering_scope *scope);
VkResult hybris_rendering_scope_begin(struct hybris_rendering_scope *scope,
    VkCommandBuffer command, const VkRenderingInfo *info, PFN_vkCmdBeginRendering begin,
    PFN_vkCmdPipelineBarrier barrier, const VkAllocationCallbacks *allocator);
VkResult hybris_rendering_scope_pause(struct hybris_rendering_scope *scope,
    VkCommandBuffer command, PFN_vkCmdEndRendering end, PFN_vkCmdPipelineBarrier barrier);
VkResult hybris_rendering_scope_resume(struct hybris_rendering_scope *scope,
    VkCommandBuffer command, PFN_vkCmdBeginRendering begin, PFN_vkCmdPipelineBarrier barrier);
VkResult hybris_rendering_scope_end(struct hybris_rendering_scope *scope,
    VkCommandBuffer command, PFN_vkCmdEndRendering end);
/* Shared by the HAL ICD and the standard loader layer. No backend state is
 * accessed here. On allocation failure no command is emitted; the adapter
 * must preserve recording structure and report the error at EndCommandBuffer. */
VkResult hybris_rendering_segments_begin(VkCommandBuffer command, const VkRenderingInfo *info,
    PFN_vkCmdBeginRendering begin, PFN_vkCmdPipelineBarrier barrier,
    const VkAllocationCallbacks *allocator);
PFN_vkVoidFunction hybris_rendering_segments_proc(const char *name);
#endif
