/* SPDX-License-Identifier: Apache-2.0 */
/* Host-only failure injection; no driver or GPU required. Compile with
 * -ffunction-sections -fdata-sections -Wl,--gc-sections and Vulkan headers. */
#include <assert.h>
#include <stdio.h>
#include "../../hybris/vulkan/compat/bc_resources.c"

static struct hybris_bc_device test_device;
static struct hybris_bc_resource test_image;
static unsigned calls;
static int fail_first;
static VkResult VKAPI_CALL test_bind_buffer(VkDevice device, uint32_t count,
    const VkBindBufferMemoryInfo *infos)
{
    (void)device;
    assert(count == 1 && infos->buffer == test_image.emulated.blocks);
    ++calls;
    return fail_first && calls == 1 ? VK_ERROR_OUT_OF_DEVICE_MEMORY : VK_SUCCESS;
}
static VkResult VKAPI_CALL test_bind_image(VkDevice device, uint32_t count,
    const VkBindImageMemoryInfo *infos)
{
    (void)device;
    assert(count == 1 && infos->image != test_image.handle);
    ++calls;
    VkResult result = fail_first && calls == 1 ? VK_ERROR_OUT_OF_DEVICE_MEMORY : VK_SUCCESS;
    for (const VkBaseInStructure *node = infos->pNext; node; node = node->pNext)
        if (node->sType == VK_STRUCTURE_TYPE_BIND_MEMORY_STATUS_KHR)
            *((const VkBindMemoryStatusKHR *)node)->pResult = result;
    return result;
}
static PFN_vkVoidFunction VKAPI_CALL resolve(VkDevice device, const char *name)
{
    (void)device;
    if (!strcmp(name, "vkBindBufferMemory2") || !strcmp(name, "vkBindBufferMemory2KHR"))
        return (PFN_vkVoidFunction)test_bind_buffer;
    assert(!strcmp(name, "vkBindImageMemory2") || !strcmp(name, "vkBindImageMemory2KHR"));
    return (PFN_vkVoidFunction)test_bind_image;
}
struct hybris_bc_device *hybris_bc_device_find(VkDevice handle)
{ (void)handle; return &test_device; }
struct hybris_bc_resource *hybris_bc_resource_find(struct hybris_bc_device *device, VkImage handle)
{ (void)device; return handle == test_image.handle ? &test_image : NULL; }

int main(void)
{
    test_device.resolver = resolve;
    test_image.handle = (VkImage)(uintptr_t)1;
    test_image.emulated.image = (VkImage)(uintptr_t)3;
    test_image.emulated.blocks = (VkBuffer)(uintptr_t)4;
    for (unsigned alias = 0; alias < 2; ++alias)
    for (unsigned reverse = 0; reverse < 2; ++reverse)
    for (fail_first = 0; fail_first < 2; ++fail_first) {
        VkResult results[2] = {VK_NOT_READY, VK_NOT_READY};
        VkBindMemoryStatusKHR statuses[2] = {
            {.sType = VK_STRUCTURE_TYPE_BIND_MEMORY_STATUS_KHR, .pResult = &results[0]},
            {.sType = VK_STRUCTURE_TYPE_BIND_MEMORY_STATUS_KHR, .pResult = &results[1]}};
        VkBindImageMemoryInfo infos[2] = {
            {.sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO, .pNext = &statuses[0],
             .image = (VkImage)(uintptr_t)(reverse ? 2 : 1)},
            {.sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO, .pNext = &statuses[1],
             .image = (VkImage)(uintptr_t)(reverse ? 1 : 2)}};
        calls = 0;
        VkResult expected = fail_first ? VK_ERROR_OUT_OF_DEVICE_MEMORY : VK_SUCCESS;
        assert(bind_images(NULL, 2, infos, alias ? "vkBindImageMemory2KHR" : "vkBindImageMemory2") == expected);
        assert(calls == 2 && results[0] == expected && results[1] == VK_SUCCESS);
        /* Optional result pointers need not be supplied. */
        infos[0].pNext = infos[1].pNext = NULL;
        calls = 0;
        assert(bind_images(NULL, 2, infos, alias ? "vkBindImageMemory2KHR" : "vkBindImageMemory2") == expected);
        assert(calls == 2);
    }
    puts("PASS: native/emulated bindings, core/KHR, success/failure, optional status");
    return 0;
}
