/* SPDX-License-Identifier: Apache-2.0 */
#define VK_NO_PROTOTYPES
#include "device_features.h"
#include "../compat/vertex_stores.h"
#include "../compat/clip_distance.h"
#include "../compat/bc_policy.h"
#include "../compat/scaled_vertex.h"
#include "../compat/shader_dispatch.h"
#include <vulkan/vk_layer.h>
#include <stdio.h>
#include <string.h>

static size_t node_size(VkStructureType type)
{
    switch (type) {
    case VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO: return sizeof(VkLayerDeviceCreateInfo);
#include "device_create_sizes.inc"
    default: return 0;
    }
}

void hybris_device_features_release(struct hybris_device_features *filtered,
    const VkAllocationCallbacks *allocator)
{
    hybris_scaled_free(allocator, filtered->extensions);
    filtered->extensions = NULL;
    VkBaseOutStructure *node = filtered->copies;
    while (node && node != filtered->suffix) {
        VkBaseOutStructure *next = node->pNext;
        hybris_scaled_free(allocator, node);
        node = next;
    }
    filtered->copies = NULL;
}

VkResult hybris_device_features_prepare(VkPhysicalDevice physical,
    const VkDeviceCreateInfo *info, const VkAllocationCallbacks *allocator,
    struct hybris_device_features *out)
{
    *out = (struct hybris_device_features){.info = *info};
    int clip = hybris_clip_active(physical);
    int stores = hybris_vertex_stores_active(physical);
    int bc = hybris_bc_physical_mask(physical) != 0;
    VkPhysicalDeviceFeatures compression = {0};
    hybris_bc_enable_transcode_features(physical, &compression);
    int transcode = compression.textureCompressionETC2 || compression.textureCompressionASTC_LDR;
    int cull = hybris_cull_active(physical);
    int polygon=hybris_shader_polygon_emulated(physical);
    int multi_viewport=hybris_shader_multi_viewport_emulated(physical);
    int robustness2=hybris_shader_robustness2_access_emulated(physical);
    if (!clip && !stores && !bc && !cull && !polygon && !multi_viewport &&
        !robustness2 && !transcode) return VK_SUCCESS;
    if (robustness2 && info->enabledExtensionCount) {
        out->extensions = hybris_scaled_alloc(allocator,
            info->enabledExtensionCount * sizeof(*out->extensions),
            VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
        if (!out->extensions) return VK_ERROR_OUT_OF_HOST_MEMORY;
        uint32_t kept = 0;
        for (uint32_t i = 0; i < info->enabledExtensionCount; ++i) {
            const char *name = info->ppEnabledExtensionNames[i];
            if (!strcmp(name, VK_EXT_ROBUSTNESS_2_EXTENSION_NAME) ||
                !strcmp(name, VK_KHR_ROBUSTNESS_2_EXTENSION_NAME)) continue;
            out->extensions[kept++] = name;
        }
        out->info.ppEnabledExtensionNames = out->extensions;
        out->info.enabledExtensionCount = kept;
    }
    if (info->pEnabledFeatures) {
        out->features = *info->pEnabledFeatures;
        if (transcode) hybris_bc_enable_transcode_features(physical, &out->features);
        if (clip) out->features.shaderClipDistance = VK_FALSE;
        if (stores) out->features.vertexPipelineStoresAndAtomics = VK_FALSE;
        if (bc) out->features.textureCompressionBC = VK_FALSE;
        if (cull) {
            out->features.shaderCullDistance = VK_FALSE;
            out->features.geometryShader = VK_TRUE;
        }
        if (polygon && out->features.fillModeNonSolid) {
            out->features.fillModeNonSolid=VK_FALSE;
            out->features.drawIndirectFirstInstance=VK_TRUE;
        }
        if (multi_viewport) out->features.multiViewport=VK_FALSE;
        if (robustness2) out->features.robustBufferAccess=VK_TRUE;
        out->info.pEnabledFeatures = &out->features;
    }
    const VkBaseInStructure *target = info->pNext;
    while (target && target->sType != VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2)
        target = target->pNext;
    if (!target) {
        if ((cull || transcode) && !info->pEnabledFeatures) {
            out->features = (VkPhysicalDeviceFeatures){.geometryShader = cull ? VK_TRUE : VK_FALSE};
            if (transcode) hybris_bc_enable_transcode_features(physical, &out->features);
            out->info.pEnabledFeatures = &out->features;
        }
        return VK_SUCCESS;
    }
    const VkPhysicalDeviceFeatures *requested = &((const VkPhysicalDeviceFeatures2 *)target)->features;
    int add_compression = (!requested->textureCompressionETC2 && compression.textureCompressionETC2) ||
        (!requested->textureCompressionASTC_LDR && compression.textureCompressionASTC_LDR);
    if (!(clip && requested->shaderClipDistance) && !(stores && requested->vertexPipelineStoresAndAtomics) &&
        !(bc && requested->textureCompressionBC) && !(cull && (requested->shaderCullDistance || !requested->geometryShader)) &&
        !(polygon && requested->fillModeNonSolid) && !(multi_viewport && requested->multiViewport) &&
        !add_compression && !robustness2)
        return VK_SUCCESS;

    /* Only copy the prefix needed to replace Features2. Preserve every other
     * payload and the untouched suffix; never write through application input
     * memory, which can be read-only or shared by concurrent device creates. */
    out->suffix = robustness2 ? NULL : target->pNext;
    VkBaseOutStructure **slot = &out->copies;
    for (const VkBaseInStructure *node = info->pNext; ; node = node->pNext) {
        size_t size = node_size(node->sType);
        if (!size) {
            fprintf(stderr, "HYBRIS_DEVICE_FEATURES unknown prefix sType=%u\n", node->sType);
            hybris_device_features_release(out, allocator);
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }
        VkBaseOutStructure *copy = hybris_scaled_alloc(allocator, size, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
        if (!copy) {
            hybris_device_features_release(out, allocator);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        memcpy(copy, node, size);
        copy->pNext = NULL;
        *slot = copy;
        slot = &copy->pNext;
        if (node == target) {
            if (transcode) hybris_bc_enable_transcode_features(physical, &((VkPhysicalDeviceFeatures2 *)copy)->features);
            if (clip) ((VkPhysicalDeviceFeatures2 *)copy)->features.shaderClipDistance = VK_FALSE;
            if (stores) ((VkPhysicalDeviceFeatures2 *)copy)->features.vertexPipelineStoresAndAtomics = VK_FALSE;
            if (bc) ((VkPhysicalDeviceFeatures2 *)copy)->features.textureCompressionBC = VK_FALSE;
            if (cull) {
                ((VkPhysicalDeviceFeatures2 *)copy)->features.shaderCullDistance = VK_FALSE;
                ((VkPhysicalDeviceFeatures2 *)copy)->features.geometryShader = VK_TRUE;
            }
            if (polygon && ((VkPhysicalDeviceFeatures2*)copy)->features.fillModeNonSolid) {
                ((VkPhysicalDeviceFeatures2*)copy)->features.fillModeNonSolid=VK_FALSE;
                ((VkPhysicalDeviceFeatures2*)copy)->features.drawIndirectFirstInstance=VK_TRUE;
            }
            if (multi_viewport) ((VkPhysicalDeviceFeatures2*)copy)->features.multiViewport=VK_FALSE;
            if (robustness2) ((VkPhysicalDeviceFeatures2*)copy)->features.robustBufferAccess=VK_TRUE;
            if (!robustness2) {
                copy->pNext = (void *)out->suffix;
                break;
            }
        }
        if (robustness2 && node->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT) {
            VkPhysicalDeviceRobustness2FeaturesEXT *features=(void*)copy;
            features->robustBufferAccess2=VK_FALSE;
            features->robustImageAccess2=VK_FALSE;
        }
        if (robustness2 && !node->pNext) break;
    }
    out->info.pNext = out->copies;
    return VK_SUCCESS;
}
