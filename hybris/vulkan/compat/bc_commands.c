/* SPDX-License-Identifier: Apache-2.0 */
#define VK_NO_PROTOTYPES
#include "bc_context.h"
#include "shader_dispatch.h"
#include <string.h>

#define PROC(name) PFN_vk##name name = (PFN_vk##name)device->resolver(device->handle, "vk" #name)
static int journal_enabled(const struct hybris_bc_device *device)
{ return device->format_mask || hybris_shader_polygon_active(device->handle); }
static VkResult VKAPI_CALL allocate_commands(VkDevice handle, const VkCommandBufferAllocateInfo *info,
    VkCommandBuffer *handles)
{
    struct hybris_bc_device *device = hybris_bc_device_find(handle);
    if (!device) return VK_ERROR_INITIALIZATION_FAILED;
    struct hybris_bc_command *first = NULL;
    for (uint32_t i = 0; i < info->commandBufferCount; ++i) {
        struct hybris_bc_command *command = hybris_bc_alloc(device, sizeof(*command));
        if (!command) {
            while (first) { command = first->next; hybris_bc_free(device, first); first = command; }
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        command->pool = info->commandPool;
        hybris_bc_command_state_init(&command->state, &device->layouts);
        if(hybris_shader_polygon_active(handle)) hybris_bc_command_state_track_graphics(&command->state);
        hybris_bc_transfer_init(&command->transfer, &device->decoder, &device->memory, &device->properties.limits);
        command->next = first;
        first = command;
    }
    PROC(AllocateCommandBuffers);
    VkResult result = AllocateCommandBuffers(handle, info, handles);
    if (result != VK_SUCCESS) {
        while (first) { struct hybris_bc_command *next = first->next; hybris_bc_free(device, first); first = next; }
        return result;
    }
    pthread_mutex_lock(&device->guard);
    for (uint32_t i = 0; i < info->commandBufferCount; ++i) {
        struct hybris_bc_command *next = first->next;
        first->handle = handles[i];
        first->next = device->commands;
        device->commands = first;
        hybris_bc_command_publish(device, first);
        first = next;
    }
    pthread_mutex_unlock(&device->guard);
    return result;
}
static void remove_command(struct hybris_bc_device *device, VkCommandBuffer handle)
{
    pthread_mutex_lock(&device->guard);
    struct hybris_bc_command **link = &device->commands;
    while (*link && (*link)->handle != handle) link = &(*link)->next;
    struct hybris_bc_command *command = *link;
    if (command) {
        *link = command->next;
        hybris_bc_command_unpublish(command);
    }
    pthread_mutex_unlock(&device->guard);
    if (command) { hybris_bc_command_retire(command); hybris_bc_free(device, command); }
}
static void VKAPI_CALL free_commands(VkDevice handle, VkCommandPool pool, uint32_t count,
    const VkCommandBuffer *commands)
{
    struct hybris_bc_device *device = hybris_bc_device_find(handle);
    if (!device) return;
    PROC(FreeCommandBuffers);
    FreeCommandBuffers(handle, pool, count, commands);
    for (uint32_t i = 0; i < count; ++i) remove_command(device, commands[i]);
}
static VkResult VKAPI_CALL begin_command(VkCommandBuffer handle, const VkCommandBufferBeginInfo *info)
{
    struct hybris_bc_device *device;
    struct hybris_bc_command *command = hybris_bc_command_find(handle, &device);
    if (!command) return VK_ERROR_INITIALIZATION_FAILED;
    PROC(BeginCommandBuffer);
    VkResult result = BeginCommandBuffer(handle, info);
    if (result == VK_SUCCESS) hybris_bc_command_retire(command);
    return result;
}
static VkResult VKAPI_CALL end_command(VkCommandBuffer handle)
{
    struct hybris_bc_device *device;
    struct hybris_bc_command *command = hybris_bc_command_find(handle, &device);
    if (!command) return VK_ERROR_INITIALIZATION_FAILED;
    PROC(EndCommandBuffer);
    VkResult result = EndCommandBuffer(handle);
    VkResult recorded = command->state.error;
    if (recorded == VK_SUCCESS) return result;
    if (recorded == VK_ERROR_OUT_OF_HOST_MEMORY || recorded == VK_ERROR_OUT_OF_DEVICE_MEMORY ||
        recorded == VK_ERROR_DEVICE_LOST) return recorded;
    /* Recording callbacks cannot return unsupported-operation errors. */
    return VK_ERROR_UNKNOWN;
}
static VkResult VKAPI_CALL reset_command(VkCommandBuffer handle, VkCommandBufferResetFlags flags)
{
    struct hybris_bc_device *device;
    struct hybris_bc_command *command = hybris_bc_command_find(handle, &device);
    if (!command) return VK_ERROR_INITIALIZATION_FAILED;
    PROC(ResetCommandBuffer);
    VkResult result = ResetCommandBuffer(handle, flags);
    if (result == VK_SUCCESS) hybris_bc_command_retire(command);
    return result;
}
/* Detach only this externally synchronized pool; release backend resources
 * outside the registry lock, which also protects unrelated command pools. */
static struct hybris_bc_command *detach_pool(struct hybris_bc_device *device, VkCommandPool pool)
{
    struct hybris_bc_command *retired = NULL;
    pthread_mutex_lock(&device->guard);
    struct hybris_bc_command **link = &device->commands;
    while (*link) {
        struct hybris_bc_command *command = *link;
        if (command->pool != pool) { link = &command->next; continue; }
        *link = command->next;
        hybris_bc_command_unpublish(command);
        command->next = retired;
        retired = command;
    }
    pthread_mutex_unlock(&device->guard);
    return retired;
}
static VkResult VKAPI_CALL reset_pool(VkDevice handle, VkCommandPool pool, VkCommandPoolResetFlags flags)
{
    struct hybris_bc_device *device = hybris_bc_device_find(handle);
    if (!device) return VK_ERROR_INITIALIZATION_FAILED;
    PROC(ResetCommandPool);
    VkResult result = ResetCommandPool(handle, pool, flags);
    if (result != VK_SUCCESS) return result;
    struct hybris_bc_command *retired = detach_pool(device, pool);
    for (struct hybris_bc_command *command = retired; command; command = command->next)
        hybris_bc_command_retire(command);
    pthread_mutex_lock(&device->guard);
    while (retired) {
        struct hybris_bc_command *next = retired->next;
        retired->next = device->commands;
        device->commands = retired;
        hybris_bc_command_publish(device, retired);
        retired = next;
    }
    pthread_mutex_unlock(&device->guard);
    return result;
}
static void VKAPI_CALL destroy_pool(VkDevice handle, VkCommandPool pool, const VkAllocationCallbacks *allocator)
{
    struct hybris_bc_device *device = hybris_bc_device_find(handle);
    if (!device) return;
    PROC(DestroyCommandPool);
    DestroyCommandPool(handle, pool, allocator);
    struct hybris_bc_command *retired = detach_pool(device, pool);
    while (retired) {
        struct hybris_bc_command *next = retired->next;
        hybris_bc_command_retire(retired);
        hybris_bc_free(device, retired);
        retired = next;
    }
}
static VkResult VKAPI_CALL create_layout(VkDevice handle, const VkPipelineLayoutCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkPipelineLayout *layout)
{
    struct hybris_bc_device *device = hybris_bc_device_find(handle);
    if (!device) return VK_ERROR_INITIALIZATION_FAILED;
    PROC(CreatePipelineLayout); PROC(DestroyPipelineLayout);
    VkResult result = CreatePipelineLayout(handle, info, allocator, layout);
    if (result != VK_SUCCESS || !journal_enabled(device)) return result;
    result = hybris_bc_layout_add(&device->layouts, *layout, info);
    if (result != VK_SUCCESS) {
        DestroyPipelineLayout(handle, *layout, allocator);
        *layout = VK_NULL_HANDLE;
    }
    return result;
}
static void VKAPI_CALL destroy_layout(VkDevice handle, VkPipelineLayout layout,
    const VkAllocationCallbacks *allocator)
{
    struct hybris_bc_device *device = hybris_bc_device_find(handle);
    if (!device) return;
    PROC(DestroyPipelineLayout);
    DestroyPipelineLayout(handle, layout, allocator);
    if (journal_enabled(device)) hybris_bc_layout_remove(&device->layouts, layout);
}
static void VKAPI_CALL bind_pipeline(VkCommandBuffer handle, VkPipelineBindPoint point, VkPipeline pipeline)
{
    struct hybris_bc_device *device;
    struct hybris_bc_command *command = hybris_bc_command_find(handle, &device);
    if (!command) return;
    PROC(CmdBindPipeline);
    CmdBindPipeline(handle, point, pipeline);
    if (journal_enabled(device)) hybris_bc_save_pipeline(&command->state, point, pipeline);
}
static void VKAPI_CALL bind_descriptors(VkCommandBuffer handle, VkPipelineBindPoint point,
    VkPipelineLayout layout, uint32_t first, uint32_t count, const VkDescriptorSet *sets,
    uint32_t offset_count, const uint32_t *offsets)
{
    struct hybris_bc_device *device;
    struct hybris_bc_command *command = hybris_bc_command_find(handle, &device);
    if (!command) return;
    PROC(CmdBindDescriptorSets);
    CmdBindDescriptorSets(handle, point, layout, first, count, sets, offset_count, offsets);
    if (journal_enabled(device))
        hybris_bc_save_descriptors(&command->state, point, layout, first, count, sets, offset_count, offsets);
}
static void VKAPI_CALL push_constants(VkCommandBuffer handle, VkPipelineLayout layout,
    VkShaderStageFlags stages, uint32_t offset, uint32_t size, const void *values)
{
    struct hybris_bc_device *device;
    struct hybris_bc_command *command = hybris_bc_command_find(handle, &device);
    if (!command) return;
    PROC(CmdPushConstants);
    CmdPushConstants(handle, layout, stages, offset, size, values);
    if (journal_enabled(device)) hybris_bc_save_push(&command->state, layout, stages, offset, size, values);
}
/* maintenance6 entry points also change the compute state temporarily used
 * by the BC decoder. Forward the complete native command, then capture the
 * same state as the original descriptor/push-constant entry points. */
static void bind_descriptors2(VkCommandBuffer handle, const VkBindDescriptorSetsInfoKHR *info, const char *name)
{
    struct hybris_bc_device *device;
    struct hybris_bc_command *command = hybris_bc_command_find(handle, &device);
    if (!command) return;
    PFN_vkCmdBindDescriptorSets2KHR bind = (PFN_vkCmdBindDescriptorSets2KHR)device->resolver(device->handle, name);
    bind(handle, info);
    int compute = !!(info->stageFlags & VK_SHADER_STAGE_COMPUTE_BIT);
    int graphics = command->state.track_graphics && !!(info->stageFlags & VK_SHADER_STAGE_ALL_GRAPHICS);
    if (journal_enabled(device) && (compute || graphics)) {
        if (!info->layout) { command->state.error = VK_ERROR_FEATURE_NOT_PRESENT; return; }
        if (compute) hybris_bc_save_descriptors(&command->state, VK_PIPELINE_BIND_POINT_COMPUTE,
            info->layout, info->firstSet, info->descriptorSetCount, info->pDescriptorSets,
            info->dynamicOffsetCount, info->pDynamicOffsets);
        if (graphics) hybris_bc_save_descriptors(&command->state, VK_PIPELINE_BIND_POINT_GRAPHICS,
            info->layout, info->firstSet, info->descriptorSetCount, info->pDescriptorSets,
            info->dynamicOffsetCount, info->pDynamicOffsets);
    }
}
static void VKAPI_CALL bind_descriptors2_khr(VkCommandBuffer handle, const VkBindDescriptorSetsInfoKHR *info)
{ bind_descriptors2(handle, info, "vkCmdBindDescriptorSets2KHR"); }
static void VKAPI_CALL bind_descriptors2_core(VkCommandBuffer handle, const VkBindDescriptorSetsInfoKHR *info)
{ bind_descriptors2(handle, info, "vkCmdBindDescriptorSets2"); }
static void push_constants2(VkCommandBuffer handle, const VkPushConstantsInfoKHR *info, const char *name)
{
    struct hybris_bc_device *device;
    struct hybris_bc_command *command = hybris_bc_command_find(handle, &device);
    if (!command) return;
    PFN_vkCmdPushConstants2KHR push = (PFN_vkCmdPushConstants2KHR)device->resolver(device->handle, name);
    push(handle, info);
    if (journal_enabled(device)) {
        if (!info->layout) { command->state.error = VK_ERROR_FEATURE_NOT_PRESENT; return; }
        hybris_bc_save_push(&command->state, info->layout, info->stageFlags,
            info->offset, info->size, info->pValues);
    }
}
static void VKAPI_CALL push_constants2_khr(VkCommandBuffer handle, const VkPushConstantsInfoKHR *info)
{ push_constants2(handle, info, "vkCmdPushConstants2KHR"); }
static void VKAPI_CALL push_constants2_core(VkCommandBuffer handle, const VkPushConstantsInfoKHR *info)
{ push_constants2(handle, info, "vkCmdPushConstants2"); }
PFN_vkVoidFunction hybris_bc_commands_proc(const char *name)
{
    static const struct { const char *name; PFN_vkVoidFunction function; } commands[] = {
#define ENTRY(name, function) {"vk" #name, (PFN_vkVoidFunction)function}
        ENTRY(AllocateCommandBuffers, allocate_commands), ENTRY(FreeCommandBuffers, free_commands),
        ENTRY(BeginCommandBuffer, begin_command), ENTRY(EndCommandBuffer, end_command),
        ENTRY(ResetCommandBuffer, reset_command), ENTRY(ResetCommandPool, reset_pool),
        ENTRY(DestroyCommandPool, destroy_pool), ENTRY(CreatePipelineLayout, create_layout),
        ENTRY(DestroyPipelineLayout, destroy_layout), ENTRY(CmdBindPipeline, bind_pipeline),
        ENTRY(CmdBindDescriptorSets, bind_descriptors), ENTRY(CmdPushConstants, push_constants),
        ENTRY(CmdBindDescriptorSets2KHR, bind_descriptors2_khr), ENTRY(CmdBindDescriptorSets2, bind_descriptors2_core),
        ENTRY(CmdPushConstants2KHR, push_constants2_khr), ENTRY(CmdPushConstants2, push_constants2_core)
#undef ENTRY
    };
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i)
        if (!strcmp(name, commands[i].name)) return commands[i].function;
    return NULL;
}
