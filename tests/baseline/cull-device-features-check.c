/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "device_features.h"

static int active;
int hybris_clip_active(VkPhysicalDevice p) { (void)p; return 0; }
int hybris_vertex_stores_active(VkPhysicalDevice p) { (void)p; return 0; }
int hybris_bc_complete(VkPhysicalDevice p) { (void)p; return 0; }
int hybris_cull_active(VkPhysicalDevice p) { (void)p; return active; }

int main(void)
{
    unsigned tested = 0;
    for (active = 0; active < 2; ++active) {
        for (unsigned path = 0; path < 3; ++path) {
            for (unsigned requested = 0; requested < 2; ++requested) {
                VkPhysicalDeviceFeatures features = {.geometryShader = requested, .robustBufferAccess = VK_TRUE};
                VkPhysicalDeviceFeatures2 f2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                    .features = features};
                VkPhysicalDeviceVulkan11Features prefix = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
                    .pNext = &f2, .multiview = VK_TRUE};
                VkBaseOutStructure suffix = {.sType = (VkStructureType)0x7ffffffe};
                f2.pNext = &suffix;
                VkDeviceCreateInfo info = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
                if (path == 1) info.pEnabledFeatures = &features;
                if (path == 2) info.pNext = &prefix;
                VkDeviceCreateInfo original = info;
                VkPhysicalDeviceFeatures2 original_f2 = f2;
                VkPhysicalDeviceVulkan11Features original_prefix = prefix;
                struct hybris_device_features prepared;
                assert(hybris_device_features_prepare(VK_NULL_HANDLE, &info, NULL, &prepared) == VK_SUCCESS);
                assert(!memcmp(&info, &original, sizeof(info)));
                assert(!memcmp(&f2, &original_f2, sizeof(f2)));
                assert(!memcmp(&prefix, &original_prefix, sizeof(prefix)));
                const VkPhysicalDeviceFeatures *out = prepared.info.pEnabledFeatures;
                if (path == 2) {
                    const VkPhysicalDeviceVulkan11Features *p = prepared.info.pNext;
                    assert(p->multiview);
                    const VkPhysicalDeviceFeatures2 *f = p->pNext;
                    assert(f->pNext == &suffix);
                    out = &f->features;
                }
                if (active || path) {
                    assert(out);
                    assert(out->geometryShader == (active || (path && requested)));
                    if (path) assert(out->robustBufferAccess);
                    assert(!out->shaderCullDistance && !out->fillModeNonSolid);
                } else assert(!out);
                hybris_device_features_release(&prepared, NULL);
                ++tested;
            }
        }
    }
    printf("CULL_DEVICE_FEATURES cases=%u application-input-unchanged=ok\n", tested);
    return 0;
}
