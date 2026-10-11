/* SPDX-License-Identifier: Apache-2.0 */
#define VK_NO_PROTOTYPES
#include "bc_command_state.h"
#include <stdlib.h>
#include <string.h>

struct hybris_bc_layout {
    VkPipelineLayout handle, clone;
    size_t references;
    struct hybris_bc_layout *next;
};
enum saved_kind { SAVED_DESCRIPTORS, SAVED_PUSH };
struct hybris_bc_saved_command {
    struct hybris_bc_saved_command *next;
    struct hybris_bc_layout *layout;
    enum saved_kind kind;
    VkPipelineBindPoint bind_point;
    uint32_t first, count, offset_count;
    VkShaderStageFlags stages;
    /* Descriptor handles followed by uint32_t offsets, or push bytes. */
    _Alignas(VkDescriptorSet) unsigned char data[];
};

void hybris_bc_layouts_init(struct hybris_bc_layouts *layouts, VkDevice device,
    PFN_vkGetDeviceProcAddr resolver)
{
    memset(layouts, 0, sizeof(*layouts));
    layouts->device = device;
    layouts->resolver = resolver;
    pthread_mutex_init(&layouts->guard, NULL);
}

void hybris_bc_layout_release(struct hybris_bc_layouts *layouts, struct hybris_bc_layout *layout)
{
    pthread_mutex_lock(&layouts->guard);
    int destroy = --layout->references == 0;
    pthread_mutex_unlock(&layouts->guard);
    if (destroy) {
        PFN_vkDestroyPipelineLayout destroy_layout =
            (PFN_vkDestroyPipelineLayout)layouts->resolver(layouts->device, "vkDestroyPipelineLayout");
        destroy_layout(layouts->device, layout->clone, NULL);
        free(layout);
    }
}

VkResult hybris_bc_layout_add(struct hybris_bc_layouts *layouts, VkPipelineLayout handle,
    const VkPipelineLayoutCreateInfo *info)
{
    struct hybris_bc_layout *layout = calloc(1, sizeof(*layout));
    if (!layout) return VK_ERROR_OUT_OF_HOST_MEMORY;
    PFN_vkCreatePipelineLayout create =
        (PFN_vkCreatePipelineLayout)layouts->resolver(layouts->device, "vkCreatePipelineLayout");
    VkResult result = create(layouts->device, info, NULL, &layout->clone);
    if (result != VK_SUCCESS) { free(layout); return result; }
    layout->handle = handle;
    layout->references = 1;
    pthread_mutex_lock(&layouts->guard);
    layout->next = layouts->head;
    layouts->head = layout;
    pthread_mutex_unlock(&layouts->guard);
    return VK_SUCCESS;
}

void hybris_bc_layout_remove(struct hybris_bc_layouts *layouts, VkPipelineLayout handle)
{
    pthread_mutex_lock(&layouts->guard);
    struct hybris_bc_layout **link = &layouts->head;
    while (*link && (*link)->handle != handle) link = &(*link)->next;
    struct hybris_bc_layout *layout = *link;
    if (layout) *link = layout->next;
    pthread_mutex_unlock(&layouts->guard);
    if (layout) hybris_bc_layout_release(layouts, layout);
}

void hybris_bc_layouts_finish(struct hybris_bc_layouts *layouts)
{
    /* All command-buffer journals must have been retired first. */
    while (layouts->head) hybris_bc_layout_remove(layouts, layouts->head->handle);
    pthread_mutex_destroy(&layouts->guard);
}

void hybris_bc_command_state_init(struct hybris_bc_command_state *state,
    struct hybris_bc_layouts *layouts)
{
    memset(state, 0, sizeof(*state));
    state->layouts = layouts;
}

void hybris_bc_command_state_reset(struct hybris_bc_command_state *state)
{
    unsigned track_graphics = state->track_graphics;
    struct hybris_bc_saved_command *saved = state->first;
    while (saved) {
        struct hybris_bc_saved_command *next = saved->next;
        hybris_bc_layout_release(state->layouts, saved->layout);
        free(saved);
        saved = next;
    }
    hybris_bc_command_state_init(state, state->layouts);
    state->track_graphics = track_graphics;
}
void hybris_bc_command_state_track_graphics(struct hybris_bc_command_state *state)
{ state->track_graphics = 1; }

static struct hybris_bc_saved_command *save(struct hybris_bc_command_state *state,
    VkPipelineLayout handle, size_t bytes)
{
    if (state->error != VK_SUCCESS) return NULL;
    if (bytes > SIZE_MAX - sizeof(struct hybris_bc_saved_command)) {
        state->error = VK_ERROR_OUT_OF_HOST_MEMORY;
        return NULL;
    }
    struct hybris_bc_saved_command *saved = calloc(1, sizeof(*saved) + bytes);
    if (!saved) { state->error = VK_ERROR_OUT_OF_HOST_MEMORY; return NULL; }
    VkResult result = hybris_bc_layout_acquire(state->layouts, handle, &saved->layout);
    if (result != VK_SUCCESS) {
        free(saved);
        state->error = result;
        return NULL;
    }
    if (state->last) state->last->next = saved;
    else state->first = saved;
    state->last = saved;
    return saved;
}

void hybris_bc_save_pipeline(struct hybris_bc_command_state *state,
    VkPipelineBindPoint bind_point, VkPipeline pipeline)
{
    if (bind_point == VK_PIPELINE_BIND_POINT_COMPUTE) state->compute_pipeline = pipeline;
    else if (state->track_graphics && bind_point == VK_PIPELINE_BIND_POINT_GRAPHICS)
        state->graphics_pipeline = pipeline;
}

void hybris_bc_save_descriptors(struct hybris_bc_command_state *state,
    VkPipelineBindPoint bind_point, VkPipelineLayout layout, uint32_t first_set,
    uint32_t count, const VkDescriptorSet *sets, uint32_t offset_count,
    const uint32_t *offsets)
{
    if (!count || (bind_point != VK_PIPELINE_BIND_POINT_COMPUTE &&
        !(state->track_graphics && bind_point == VK_PIPELINE_BIND_POINT_GRAPHICS))) return;
    uint64_t bytes = (uint64_t)count * sizeof(*sets) + (uint64_t)offset_count * sizeof(*offsets);
    if (bytes > SIZE_MAX) { state->error = VK_ERROR_OUT_OF_HOST_MEMORY; return; }
    struct hybris_bc_saved_command *saved = save(state, layout, (size_t)bytes);
    if (!saved) return;
    saved->kind = SAVED_DESCRIPTORS;
    saved->bind_point = bind_point;
    saved->first = first_set;
    saved->count = count;
    saved->offset_count = offset_count;
    memcpy(saved->data, sets, (size_t)count * sizeof(*sets));
    if (offset_count)
        memcpy(saved->data + (size_t)count * sizeof(*sets), offsets, (size_t)offset_count * sizeof(*offsets));
}

void hybris_bc_save_push(struct hybris_bc_command_state *state, VkPipelineLayout layout,
    VkShaderStageFlags stages, uint32_t offset, uint32_t size, const void *values)
{
    /* Keep all stages in order: a range may cover graphics and compute, and
     * replay must preserve later graphics writes to the same byte positions. */
    struct hybris_bc_saved_command *saved = save(state, layout, size);
    if (!saved) return;
    saved->kind = SAVED_PUSH;
    saved->stages = stages;
    saved->first = offset;
    saved->count = size;
    if (size) memcpy(saved->data, values, size);
}

static void restore(struct hybris_bc_command_state *state, VkCommandBuffer command, VkPipelineBindPoint bind_point)
{
    if (state->error != VK_SUCCESS) return;
    PFN_vkGetDeviceProcAddr resolver = state->layouts->resolver;
    VkDevice device = state->layouts->device;
    PFN_vkCmdBindPipeline pipeline = (PFN_vkCmdBindPipeline)resolver(device, "vkCmdBindPipeline");
    PFN_vkCmdBindDescriptorSets descriptors =
        (PFN_vkCmdBindDescriptorSets)resolver(device, "vkCmdBindDescriptorSets");
    PFN_vkCmdPushConstants push = (PFN_vkCmdPushConstants)resolver(device, "vkCmdPushConstants");
    VkPipeline handle = bind_point == VK_PIPELINE_BIND_POINT_COMPUTE ? state->compute_pipeline : state->graphics_pipeline;
    if (handle) pipeline(command, bind_point, handle);
    for (const struct hybris_bc_saved_command *saved = state->first; saved; saved = saved->next) {
        if (saved->kind == SAVED_DESCRIPTORS) {
            if (saved->bind_point != bind_point) continue;
            descriptors(command, bind_point, saved->layout->clone,
                saved->first, saved->count, (const VkDescriptorSet *)saved->data,
                saved->offset_count,
                (const uint32_t *)(saved->data + (size_t)saved->count * sizeof(VkDescriptorSet)));
        }
    }
    /* Preserve the complete stage mask required by each layout's ranges.
     * Restore unrelated stages first, then the stages about to execute. Some
     * backends share push storage across bind points; replaying an unrelated
     * layout last would overwrite the active pipeline's restored bytes. */
    VkShaderStageFlags active = bind_point == VK_PIPELINE_BIND_POINT_COMPUTE ?
        VK_SHADER_STAGE_COMPUTE_BIT : VK_SHADER_STAGE_ALL_GRAPHICS;
    for (unsigned pass = 0; pass < 2; ++pass)
        for (const struct hybris_bc_saved_command *saved = state->first; saved; saved = saved->next)
            if (saved->kind == SAVED_PUSH && !!(saved->stages & active) == (int)pass)
                push(command, saved->layout->clone, saved->stages, saved->first, saved->count, saved->data);
}
void hybris_bc_restore_compute(struct hybris_bc_command_state *state, VkCommandBuffer command)
{ restore(state, command, VK_PIPELINE_BIND_POINT_COMPUTE); }
void hybris_bc_restore_graphics(struct hybris_bc_command_state *state, VkCommandBuffer command)
{ if (state->track_graphics) restore(state, command, VK_PIPELINE_BIND_POINT_GRAPHICS); }

VkResult hybris_bc_layout_acquire(struct hybris_bc_layouts *layouts, VkPipelineLayout handle,
    struct hybris_bc_layout **out)
{
    *out = NULL;
    pthread_mutex_lock(&layouts->guard);
    struct hybris_bc_layout *layout = layouts->head;
    while (layout && layout->handle != handle) layout = layout->next;
    VkResult result = layout ? VK_ERROR_OUT_OF_HOST_MEMORY : VK_ERROR_INITIALIZATION_FAILED;
    if (layout && layout->references != SIZE_MAX) {
        ++layout->references; *out = layout; result = VK_SUCCESS;
    }
    pthread_mutex_unlock(&layouts->guard);
    return result;
}
VkResult hybris_bc_visit_graphics(const struct hybris_bc_command_state *state,
    const struct hybris_bc_graphics_visitor *visitor)
{
    if (state->error != VK_SUCCESS) return state->error;
    if (!state->track_graphics || !visitor) return VK_ERROR_FEATURE_NOT_PRESENT;
    for (const struct hybris_bc_saved_command *s = state->first; s; s = s->next) {
        VkResult result = VK_SUCCESS;
        if (s->kind == SAVED_DESCRIPTORS && s->bind_point == VK_PIPELINE_BIND_POINT_GRAPHICS && visitor->descriptors)
            result = visitor->descriptors(visitor->user, s->layout, s->first, s->count,
                (const VkDescriptorSet *)s->data, s->offset_count,
                (const uint32_t *)(s->data + (size_t)s->count * sizeof(VkDescriptorSet)));
        else if (s->kind == SAVED_PUSH && (s->stages & VK_SHADER_STAGE_VERTEX_BIT) && visitor->push)
            result = visitor->push(visitor->user, s->layout, s->stages, s->first, s->count, s->data);
        if (result != VK_SUCCESS) return result;
    }
    return VK_SUCCESS;
}
