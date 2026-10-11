/* SPDX-License-Identifier: Apache-2.0 */
/* Exercise the real capability predicate with incomplete driver domains. */
#define VK_NO_PROTOTYPES
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../hybris/vulkan/compat/bc_policy.c"

static unsigned failure;

static void VKAPI_CALL properties(VkPhysicalDevice physical, VkPhysicalDeviceProperties *out)
{
    (void)physical;
    memset(out, 0, sizeof(*out));
    out->apiVersion = VK_API_VERSION_1_3;
    out->limits.maxStorageBufferRange = 65536;
    out->limits.minStorageBufferOffsetAlignment = 64;
}
static void VKAPI_CALL queues(VkPhysicalDevice physical, uint32_t *count, VkQueueFamilyProperties *out)
{
    (void)physical;
    *count = 1;
    if (out) *out = (VkQueueFamilyProperties){
        .queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT,
        .queueCount = 1, .minImageTransferGranularity = {1, 1, 1}};
}
static void VKAPI_CALL formats(VkPhysicalDevice physical, VkFormat format, VkFormatProperties *out)
{
    (void)physical;
    *out = (VkFormatProperties){0};
    if (format >= VK_FORMAT_BC1_RGB_UNORM_BLOCK && format <= VK_FORMAT_BC7_SRGB_BLOCK) return;
    out->optimalTilingFeatures = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT | VK_FORMAT_FEATURE_BLIT_SRC_BIT |
        VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    if (failure == 1 && format == VK_FORMAT_R16_UNORM)
        out->optimalTilingFeatures &= ~VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if (failure == 2 && format == VK_FORMAT_R16G16_UNORM)
        out->optimalTilingFeatures &= ~VK_FORMAT_FEATURE_BLIT_SRC_BIT;
    if (failure == 3 && format == VK_FORMAT_R8G8B8_UNORM)
        out->optimalTilingFeatures = 0;
}
static VkResult VKAPI_CALL images(VkPhysicalDevice physical, VkFormat format, VkImageType type,
    VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags, VkImageFormatProperties *out)
{
    (void)physical; (void)format; (void)tiling; (void)usage; (void)flags;
    *out = (VkImageFormatProperties){.maxExtent = {4096, 4096, type == VK_IMAGE_TYPE_3D ? 4096 : 1},
        .maxMipLevels = 13, .maxArrayLayers = type == VK_IMAGE_TYPE_3D ? 1 : 256,
        .sampleCounts = VK_SAMPLE_COUNT_1_BIT, .maxResourceSize = 1u << 30};
    if (failure == 4 && type == VK_IMAGE_TYPE_3D) return VK_ERROR_FORMAT_NOT_SUPPORTED;
    if (failure == 5) out->sampleCounts = 0;
    return VK_SUCCESS;
}
static PFN_vkVoidFunction VKAPI_CALL resolve(VkInstance instance, const char *name)
{
    (void)instance;
    if (!strcmp(name, "vkGetPhysicalDeviceProperties")) return (PFN_vkVoidFunction)properties;
    if (!strcmp(name, "vkGetPhysicalDeviceQueueFamilyProperties")) return (PFN_vkVoidFunction)queues;
    if (!strcmp(name, "vkGetPhysicalDeviceFormatProperties")) return (PFN_vkVoidFunction)formats;
    if (!strcmp(name, "vkGetPhysicalDeviceImageFormatProperties")) return (PFN_vkVoidFunction)images;
    assert(!"Unexpected driver query");
    return NULL;
}
int hybris_layer_lookup_physical(VkPhysicalDevice physical, struct hybris_layer_physical *out)
{
    (void)physical;
    *out = (struct hybris_layer_physical){.api_version = VK_API_VERSION_1_3, .resolver = resolve};
    return 1;
}
int main(void)
{
    VkPhysicalDevice physical = (VkPhysicalDevice)(uintptr_t)1;
    assert(hybris_bc_complete(physical));
    for (failure = 1; failure <= 5; ++failure) assert(!hybris_bc_complete(physical));
    failure = 0;
    assert(hybris_bc_complete(physical));
    puts("PASS: complete BC domain, missing linear/blit/RGB/3D/sample support");
    return 0;
}
