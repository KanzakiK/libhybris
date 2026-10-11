/* SPDX-License-Identifier: Apache-2.0 */
#ifndef HYBRIS_POLYGON_LAYOUTS_H
#define HYBRIS_POLYGON_LAYOUTS_H
#include <vulkan/vulkan.h>
/* Immutable application declarations, independent of destroyed/reused layout
 * handles and caller arrays. No driver resources or descriptor contents owned.
 * Unsupported extension chains have no snapshot: select whole filled draw. */
struct hybris_polygon_layout {
    VkPipelineLayoutCreateInfo info;
    VkDescriptorSetLayoutCreateInfo *sets;
};
const struct hybris_polygon_layout *hybris_polygon_layout_acquire(VkDevice device,
    VkPipelineLayout layout);
void hybris_polygon_layout_release(const struct hybris_polygon_layout *layout);
void hybris_polygon_layouts_release_device(VkDevice device);
PFN_vkVoidFunction hybris_polygon_layouts_proc(const char *name);
#endif
