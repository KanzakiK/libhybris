/* SPDX-License-Identifier: Apache-2.0 */
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DUMMY_BUFFER_SIZE 65536

struct instance_state {
    VkInstance handle;
    void *key;
    PFN_vkGetInstanceProcAddr next_get_instance_proc_addr;
    PFN_GetPhysicalDeviceProcAddr next_get_physical_device_proc_addr;
    struct instance_state *next;
};

struct device_state {
    VkDevice handle;
    void *key;
    VkPhysicalDevice physical_device;
    PFN_vkGetInstanceProcAddr next_get_instance_proc_addr;
    PFN_vkGetDeviceProcAddr next_get_device_proc_addr;
    VkBuffer dummy_buffer;
    VkDeviceMemory dummy_buffer_memory;
    VkBufferView dummy_buffer_view;
    VkImage dummy_image;
    VkDeviceMemory dummy_image_memory;
    VkImageView dummy_image_view;
    VkSampler dummy_sampler;
    struct device_state *next;
};

struct template_state {
    VkDevice device;
    VkDescriptorUpdateTemplate handle;
    VkDescriptorUpdateTemplateType type;
    VkPipelineBindPoint pipeline_bind_point;
    int khr;
    uint32_t entry_count;
    VkDescriptorUpdateTemplateEntry *entries;
    struct template_state *next;
};

static pthread_mutex_t state_lock = PTHREAD_MUTEX_INITIALIZER;
static struct instance_state *instances;
static struct device_state *devices;
static struct template_state *templates;

static void *dispatch_key(const void *handle)
{
    void *key = NULL;
    if (handle)
        memcpy(&key, handle, sizeof(key));
    return key;
}

static int find_instance(const void *dispatchable, struct instance_state *out)
{
    void *key = dispatch_key(dispatchable);
    int found = 0;

    pthread_mutex_lock(&state_lock);
    for (struct instance_state *state = instances; state; state = state->next) {
        if (state->key == key) {
            *out = *state;
            found = 1;
            break;
        }
    }
    pthread_mutex_unlock(&state_lock);
    return found;
}

static int find_device(const void *dispatchable, struct device_state *out)
{
    void *key = dispatch_key(dispatchable);
    int found = 0;

    pthread_mutex_lock(&state_lock);
    for (struct device_state *state = devices; state; state = state->next) {
        if (state->key == key) {
            *out = *state;
            found = 1;
            break;
        }
    }
    pthread_mutex_unlock(&state_lock);
    return found;
}

static int find_template(VkDevice device, VkDescriptorUpdateTemplate handle,
                         struct template_state *out)
{
    int found = 0;

    pthread_mutex_lock(&state_lock);
    for (struct template_state *state = templates; state; state = state->next) {
        if (state->device == device && state->handle == handle) {
            *out = *state;
            found = 1;
            break;
        }
    }
    pthread_mutex_unlock(&state_lock);
    return found;
}

static void free_device_templates(VkDevice device)
{
    struct template_state *retired = NULL;

    pthread_mutex_lock(&state_lock);
    for (struct template_state **slot = &templates; *slot;) {
        if ((*slot)->device == device) {
            struct template_state *state = *slot;
            *slot = state->next;
            state->next = retired;
            retired = state;
        } else {
            slot = &(*slot)->next;
        }
    }
    pthread_mutex_unlock(&state_lock);

    while (retired) {
        struct template_state *next = retired->next;
        free(retired->entries);
        free(retired);
        retired = next;
    }
}

static VkResult VKAPI_CALL create_instance(
    const VkInstanceCreateInfo *info,
    const VkAllocationCallbacks *allocator,
    VkInstance *instance)
{
    VkLayerInstanceCreateInfo *link = (void *)info->pNext;
    while (link && (link->sType != VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO ||
                    link->function != VK_LAYER_LINK_INFO))
        link = (void *)link->pNext;
    if (!link || !link->u.pLayerInfo)
        return VK_ERROR_INITIALIZATION_FAILED;

    PFN_vkGetInstanceProcAddr next_get_instance_proc_addr =
        link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_GetPhysicalDeviceProcAddr next_get_physical_device_proc_addr =
        link->u.pLayerInfo->pfnNextGetPhysicalDeviceProcAddr;
    PFN_vkCreateInstance create = (PFN_vkCreateInstance)
        next_get_instance_proc_addr(VK_NULL_HANDLE, "vkCreateInstance");
    if (!create)
        return VK_ERROR_INITIALIZATION_FAILED;

    struct instance_state *state = calloc(1, sizeof(*state));
    if (!state)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    state->next_get_instance_proc_addr = next_get_instance_proc_addr;
    state->next_get_physical_device_proc_addr = next_get_physical_device_proc_addr;

    link->u.pLayerInfo = link->u.pLayerInfo->pNext;
    VkResult result = create(info, allocator, instance);
    if (result != VK_SUCCESS) {
        free(state);
        return result;
    }

    state->handle = *instance;
    state->key = dispatch_key(*instance);
    pthread_mutex_lock(&state_lock);
    state->next = instances;
    instances = state;
    pthread_mutex_unlock(&state_lock);
    return VK_SUCCESS;
}

static VkResult VKAPI_CALL enumerate_device_extensions(
    VkPhysicalDevice physical_device,
    const char *layer_name,
    uint32_t *count,
    VkExtensionProperties *properties)
{
    struct instance_state state;
    if (!find_instance(physical_device, &state))
        return VK_ERROR_INITIALIZATION_FAILED;

    PFN_vkEnumerateDeviceExtensionProperties enumerate =
        (PFN_vkEnumerateDeviceExtensionProperties)
        state.next_get_instance_proc_addr(state.handle,
            "vkEnumerateDeviceExtensionProperties");
    if (!enumerate)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (layer_name || !count)
        return enumerate(physical_device, layer_name, count, properties);

    uint32_t available = 0;
    VkResult result = enumerate(physical_device, NULL, &available, NULL);
    if (result != VK_SUCCESS && result != VK_INCOMPLETE)
        return result;

    VkExtensionProperties *all = available
        ? calloc(available, sizeof(*all))
        : NULL;
    if (available && !all)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    if (available) {
        result = enumerate(physical_device, NULL, &available, all);
        if (result != VK_SUCCESS && result != VK_INCOMPLETE) {
            free(all);
            return result;
        }
    }

    int has_robustness2 = 0;
    for (uint32_t i = 0; i < available; ++i) {
        if (!strcmp(all[i].extensionName, VK_EXT_ROBUSTNESS_2_EXTENSION_NAME))
            has_robustness2 = 1;
    }

    uint32_t total = available + (has_robustness2 ? 0 : 1);
    if (!properties) {
        *count = total;
        free(all);
        return VK_SUCCESS;
    }

    uint32_t capacity = *count;
    uint32_t written = 0;
    for (uint32_t i = 0; i < available && written < capacity; ++i)
        properties[written++] = all[i];
    if (!has_robustness2 && written < capacity) {
        VkExtensionProperties added = {0};
        strncpy(added.extensionName, VK_EXT_ROBUSTNESS_2_EXTENSION_NAME,
            sizeof(added.extensionName) - 1);
        added.specVersion = 1;
        properties[written++] = added;
    }
    free(all);
    *count = written;
    return written < total ? VK_INCOMPLETE : VK_SUCCESS;
}

static void expose_null_descriptor(VkBaseOutStructure *node)
{
    while (node) {
        if (node->sType ==
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT) {
            VkPhysicalDeviceRobustness2FeaturesEXT *features =
                (VkPhysicalDeviceRobustness2FeaturesEXT *)node;
            features->nullDescriptor = VK_TRUE;
            return;
        }
        node = node->pNext;
    }
}

static void VKAPI_CALL get_physical_device_features2(
    VkPhysicalDevice physical_device,
    VkPhysicalDeviceFeatures2 *features)
{
    struct instance_state state;
    if (!find_instance(physical_device, &state))
        return;
    PFN_vkGetPhysicalDeviceFeatures2 next =
        (PFN_vkGetPhysicalDeviceFeatures2)
        state.next_get_instance_proc_addr(state.handle,
            "vkGetPhysicalDeviceFeatures2");
    if (!next)
        return;
    next(physical_device, features);
    expose_null_descriptor((VkBaseOutStructure *)features->pNext);
}

static void VKAPI_CALL get_physical_device_features2_khr(
    VkPhysicalDevice physical_device,
    VkPhysicalDeviceFeatures2 *features)
{
    struct instance_state state;
    if (!find_instance(physical_device, &state))
        return;
    PFN_vkGetPhysicalDeviceFeatures2KHR next =
        (PFN_vkGetPhysicalDeviceFeatures2KHR)
        state.next_get_instance_proc_addr(state.handle,
            "vkGetPhysicalDeviceFeatures2KHR");
    if (!next) {
        get_physical_device_features2(physical_device, features);
        return;
    }
    next(physical_device, features);
    expose_null_descriptor((VkBaseOutStructure *)features->pNext);
}

static PFN_vkVoidFunction device_proc(const struct device_state *state,
                                      const char *name)
{
    return state->next_get_device_proc_addr(state->handle, name);
}

static void destroy_dummies(struct device_state *state,
                            const VkAllocationCallbacks *allocator)
{
    PFN_vkDestroySampler destroy_sampler =
        (PFN_vkDestroySampler)device_proc(state, "vkDestroySampler");
    PFN_vkDestroyImageView destroy_image_view =
        (PFN_vkDestroyImageView)device_proc(state, "vkDestroyImageView");
    PFN_vkDestroyImage destroy_image =
        (PFN_vkDestroyImage)device_proc(state, "vkDestroyImage");
    PFN_vkFreeMemory free_memory =
        (PFN_vkFreeMemory)device_proc(state, "vkFreeMemory");
    PFN_vkDestroyBufferView destroy_buffer_view =
        (PFN_vkDestroyBufferView)device_proc(state, "vkDestroyBufferView");
    PFN_vkDestroyBuffer destroy_buffer =
        (PFN_vkDestroyBuffer)device_proc(state, "vkDestroyBuffer");

    if (state->dummy_sampler && destroy_sampler)
        destroy_sampler(state->handle, state->dummy_sampler, allocator);
    state->dummy_sampler = VK_NULL_HANDLE;
    if (state->dummy_image_view && destroy_image_view)
        destroy_image_view(state->handle, state->dummy_image_view, allocator);
    state->dummy_image_view = VK_NULL_HANDLE;
    if (state->dummy_image && destroy_image)
        destroy_image(state->handle, state->dummy_image, allocator);
    state->dummy_image = VK_NULL_HANDLE;
    if (state->dummy_image_memory && free_memory)
        free_memory(state->handle, state->dummy_image_memory, allocator);
    state->dummy_image_memory = VK_NULL_HANDLE;
    if (state->dummy_buffer_view && destroy_buffer_view)
        destroy_buffer_view(state->handle, state->dummy_buffer_view, allocator);
    state->dummy_buffer_view = VK_NULL_HANDLE;
    if (state->dummy_buffer && destroy_buffer)
        destroy_buffer(state->handle, state->dummy_buffer, allocator);
    state->dummy_buffer = VK_NULL_HANDLE;
    if (state->dummy_buffer_memory && free_memory)
        free_memory(state->handle, state->dummy_buffer_memory, allocator);
    state->dummy_buffer_memory = VK_NULL_HANDLE;
}

static uint32_t select_memory_type(
    VkPhysicalDevice physical_device,
    PFN_vkGetInstanceProcAddr get_instance_proc_addr,
    VkInstance instance,
    uint32_t type_bits,
    VkMemoryPropertyFlags required)
{
    PFN_vkGetPhysicalDeviceMemoryProperties get_properties =
        (PFN_vkGetPhysicalDeviceMemoryProperties)
        get_instance_proc_addr(instance, "vkGetPhysicalDeviceMemoryProperties");
    if (!get_properties)
        return UINT32_MAX;

    VkPhysicalDeviceMemoryProperties properties;
    get_properties(physical_device, &properties);
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
        if ((type_bits & (1u << i)) &&
            (properties.memoryTypes[i].propertyFlags & required) == required)
            return i;
    }
    return UINT32_MAX;
}

static VkResult create_dummies(struct device_state *state,
                               VkInstance instance,
                               const VkAllocationCallbacks *allocator)
{
    PFN_vkCreateBuffer create_buffer =
        (PFN_vkCreateBuffer)device_proc(state, "vkCreateBuffer");
    PFN_vkBindBufferMemory bind_buffer_memory =
        (PFN_vkBindBufferMemory)device_proc(state, "vkBindBufferMemory");
    PFN_vkGetBufferMemoryRequirements get_buffer_memory_requirements =
        (PFN_vkGetBufferMemoryRequirements)
        device_proc(state, "vkGetBufferMemoryRequirements");
    PFN_vkCreateBufferView create_buffer_view =
        (PFN_vkCreateBufferView)device_proc(state, "vkCreateBufferView");
    PFN_vkCreateImage create_image =
        (PFN_vkCreateImage)device_proc(state, "vkCreateImage");
    PFN_vkBindImageMemory bind_image_memory =
        (PFN_vkBindImageMemory)device_proc(state, "vkBindImageMemory");
    PFN_vkGetImageMemoryRequirements get_image_memory_requirements =
        (PFN_vkGetImageMemoryRequirements)
        device_proc(state, "vkGetImageMemoryRequirements");
    PFN_vkCreateImageView create_image_view =
        (PFN_vkCreateImageView)device_proc(state, "vkCreateImageView");
    PFN_vkCreateSampler create_sampler =
        (PFN_vkCreateSampler)device_proc(state, "vkCreateSampler");
    PFN_vkAllocateMemory allocate_memory =
        (PFN_vkAllocateMemory)device_proc(state, "vkAllocateMemory");
    PFN_vkMapMemory map_memory =
        (PFN_vkMapMemory)device_proc(state, "vkMapMemory");
    PFN_vkUnmapMemory unmap_memory =
        (PFN_vkUnmapMemory)device_proc(state, "vkUnmapMemory");
    if (!create_buffer || !bind_buffer_memory ||
        !get_buffer_memory_requirements || !create_buffer_view ||
        !create_image || !bind_image_memory ||
        !get_image_memory_requirements || !create_image_view ||
        !create_sampler || !allocate_memory)
        return VK_ERROR_INITIALIZATION_FAILED;

    VkResult result;
    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = DUMMY_BUFFER_SIZE,
        .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                 VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT |
                 VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT |
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    result = create_buffer(state->handle, &buffer_info, allocator,
                           &state->dummy_buffer);
    if (result != VK_SUCCESS)
        goto fail;

    VkMemoryRequirements buffer_requirements;
    get_buffer_memory_requirements(state->handle, state->dummy_buffer,
                                   &buffer_requirements);
    uint32_t buffer_type = select_memory_type(
        state->physical_device, state->next_get_instance_proc_addr, instance,
        buffer_requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    int buffer_is_mapped = buffer_type != UINT32_MAX;
    if (buffer_type == UINT32_MAX)
        buffer_type = select_memory_type(
            state->physical_device, state->next_get_instance_proc_addr,
            instance, buffer_requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (buffer_type == UINT32_MAX) {
        result = VK_ERROR_FEATURE_NOT_PRESENT;
        goto fail;
    }
    VkMemoryAllocateInfo allocation = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = buffer_requirements.size,
        .memoryTypeIndex = buffer_type,
    };
    result = allocate_memory(state->handle, &allocation, allocator,
                             &state->dummy_buffer_memory);
    if (result != VK_SUCCESS)
        goto fail;
    result = bind_buffer_memory(state->handle, state->dummy_buffer,
                                state->dummy_buffer_memory, 0);
    if (result != VK_SUCCESS)
        goto fail;
    if (buffer_is_mapped && map_memory && unmap_memory) {
        void *mapped = NULL;
        if (map_memory(state->handle, state->dummy_buffer_memory, 0,
                       VK_WHOLE_SIZE, 0, &mapped) == VK_SUCCESS) {
            memset(mapped, 0, (size_t)buffer_requirements.size);
            unmap_memory(state->handle, state->dummy_buffer_memory);
        }
    }

    VkBufferViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,
        .buffer = state->dummy_buffer,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .offset = 0,
        .range = VK_WHOLE_SIZE,
    };
    result = create_buffer_view(state->handle, &view_info, allocator,
                                &state->dummy_buffer_view);
    if (result != VK_SUCCESS) {
        view_info.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        result = create_buffer_view(state->handle, &view_info, allocator,
                                    &state->dummy_buffer_view);
        if (result != VK_SUCCESS) {
            state->dummy_buffer_view = VK_NULL_HANDLE;
            result = VK_SUCCESS;
        }
    }

    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {1, 1, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT |
                 VK_IMAGE_USAGE_STORAGE_BIT |
                 VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    result = create_image(state->handle, &image_info, allocator,
                          &state->dummy_image);
    if (result != VK_SUCCESS)
        goto fail;

    VkMemoryRequirements image_requirements;
    get_image_memory_requirements(state->handle, state->dummy_image,
                                  &image_requirements);
    uint32_t image_type = select_memory_type(
        state->physical_device, state->next_get_instance_proc_addr, instance,
        image_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (image_type == UINT32_MAX) {
        result = VK_ERROR_FEATURE_NOT_PRESENT;
        goto fail;
    }
    allocation = (VkMemoryAllocateInfo){
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = image_requirements.size,
        .memoryTypeIndex = image_type,
    };
    result = allocate_memory(state->handle, &allocation, allocator,
                             &state->dummy_image_memory);
    if (result != VK_SUCCESS)
        goto fail;
    result = bind_image_memory(state->handle, state->dummy_image,
                               state->dummy_image_memory, 0);
    if (result != VK_SUCCESS)
        goto fail;

    VkImageViewCreateInfo image_view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = state->dummy_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    result = create_image_view(state->handle, &image_view_info, allocator,
                               &state->dummy_image_view);
    if (result != VK_SUCCESS)
        goto fail;

    VkSamplerCreateInfo sampler_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .maxLod = 0.0f,
    };
    result = create_sampler(state->handle, &sampler_info, allocator,
                            &state->dummy_sampler);
    if (result != VK_SUCCESS)
        goto fail;
    return VK_SUCCESS;

fail:
    destroy_dummies(state, allocator);
    return result;
}

static VkResult VKAPI_CALL create_device(
    VkPhysicalDevice physical_device,
    const VkDeviceCreateInfo *info,
    const VkAllocationCallbacks *allocator,
    VkDevice *device)
{
    struct instance_state instance;
    if (!find_instance(physical_device, &instance))
        return VK_ERROR_INITIALIZATION_FAILED;

    VkLayerDeviceCreateInfo *link = (void *)info->pNext;
    while (link && (link->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO ||
                    link->function != VK_LAYER_LINK_INFO))
        link = (void *)link->pNext;
    if (!link || !link->u.pLayerInfo)
        return VK_ERROR_INITIALIZATION_FAILED;

    PFN_vkCreateDevice create = (PFN_vkCreateDevice)
        link->u.pLayerInfo->pfnNextGetInstanceProcAddr(
            instance.handle, "vkCreateDevice");
    PFN_vkGetDeviceProcAddr next_get_device_proc_addr =
        link->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    if (!create || !next_get_device_proc_addr)
        return VK_ERROR_INITIALIZATION_FAILED;

    VkDeviceCreateInfo filtered = *info;
    const char **extensions = NULL;
    uint32_t extension_count = info->enabledExtensionCount;
    if (extension_count && info->ppEnabledExtensionNames) {
        extensions = calloc(extension_count, sizeof(*extensions));
        if (!extensions)
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        uint32_t kept = 0;
        for (uint32_t i = 0; i < extension_count; ++i) {
            if (strcmp(info->ppEnabledExtensionNames[i],
                       VK_EXT_ROBUSTNESS_2_EXTENSION_NAME))
                extensions[kept++] = info->ppEnabledExtensionNames[i];
        }
        filtered.enabledExtensionCount = kept;
        filtered.ppEnabledExtensionNames = extensions;
    }

    link->u.pLayerInfo = link->u.pLayerInfo->pNext;
    VkResult result = create(physical_device, &filtered, allocator, device);
    free(extensions);
    if (result != VK_SUCCESS)
        return result;

    struct device_state *state = calloc(1, sizeof(*state));
    if (!state) {
        PFN_vkDestroyDevice destroy = (PFN_vkDestroyDevice)
            next_get_device_proc_addr(*device, "vkDestroyDevice");
        if (destroy)
            destroy(*device, allocator);
        *device = VK_NULL_HANDLE;
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    state->handle = *device;
    state->key = dispatch_key(*device);
    state->physical_device = physical_device;
    state->next_get_instance_proc_addr = instance.next_get_instance_proc_addr;
    state->next_get_device_proc_addr = next_get_device_proc_addr;
    result = create_dummies(state, instance.handle, allocator);
    if (result != VK_SUCCESS) {
        free(state);
        PFN_vkDestroyDevice destroy = (PFN_vkDestroyDevice)
            next_get_device_proc_addr(*device, "vkDestroyDevice");
        if (destroy)
            destroy(*device, allocator);
        *device = VK_NULL_HANDLE;
        return result;
    }
    pthread_mutex_lock(&state_lock);
    state->next = devices;
    devices = state;
    pthread_mutex_unlock(&state_lock);
    return VK_SUCCESS;
}

static void VKAPI_CALL destroy_device(
    VkDevice device,
    const VkAllocationCallbacks *allocator)
{
    struct device_state state;
    if (!find_device(device, &state))
        return;

    pthread_mutex_lock(&state_lock);
    for (struct device_state **slot = &devices; *slot; slot = &(*slot)->next) {
        if ((*slot)->key == state.key) {
            struct device_state *retired = *slot;
            *slot = retired->next;
            pthread_mutex_unlock(&state_lock);
            free_device_templates(device);
            destroy_dummies(retired, allocator);
            PFN_vkDestroyDevice destroy = (PFN_vkDestroyDevice)
                retired->next_get_device_proc_addr(device, "vkDestroyDevice");
            if (destroy)
                destroy(device, allocator);
            free(retired);
            return;
        }
    }
    pthread_mutex_unlock(&state_lock);
}

static void VKAPI_CALL destroy_instance(
    VkInstance instance,
    const VkAllocationCallbacks *allocator)
{
    struct instance_state state;
    if (!find_instance(instance, &state))
        return;

    pthread_mutex_lock(&state_lock);
    for (struct instance_state **slot = &instances; *slot; slot = &(*slot)->next) {
        if ((*slot)->key == state.key) {
            struct instance_state *retired = *slot;
            *slot = retired->next;
            pthread_mutex_unlock(&state_lock);
            PFN_vkDestroyInstance destroy = (PFN_vkDestroyInstance)
                retired->next_get_instance_proc_addr(instance,
                    "vkDestroyInstance");
            if (destroy)
                destroy(instance, allocator);
            free(retired);
            return;
        }
    }
    pthread_mutex_unlock(&state_lock);
}

enum descriptor_data_kind {
    DESCRIPTOR_DATA_NONE,
    DESCRIPTOR_DATA_IMAGE,
    DESCRIPTOR_DATA_BUFFER,
    DESCRIPTOR_DATA_VIEW,
    DESCRIPTOR_DATA_UNKNOWN,
};

static enum descriptor_data_kind descriptor_data_kind(VkDescriptorType type)
{
    switch (type) {
    case VK_DESCRIPTOR_TYPE_SAMPLER:
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
    case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
        return DESCRIPTOR_DATA_IMAGE;
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
        return DESCRIPTOR_DATA_BUFFER;
    case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
    case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
        return DESCRIPTOR_DATA_VIEW;
    case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK:
        return DESCRIPTOR_DATA_NONE;
    default:
        return DESCRIPTOR_DATA_UNKNOWN;
    }
}

static size_t descriptor_element_size(enum descriptor_data_kind kind)
{
    switch (kind) {
    case DESCRIPTOR_DATA_IMAGE:
        return sizeof(VkDescriptorImageInfo);
    case DESCRIPTOR_DATA_BUFFER:
        return sizeof(VkDescriptorBufferInfo);
    case DESCRIPTOR_DATA_VIEW:
        return sizeof(VkBufferView);
    default:
        return 0;
    }
}

static void sanitize_image_info(const struct device_state *state,
                                VkDescriptorType type,
                                VkDescriptorImageInfo *info)
{
    int needs_sampler =
        type == VK_DESCRIPTOR_TYPE_SAMPLER ||
        type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    int needs_view =
        type != VK_DESCRIPTOR_TYPE_SAMPLER;

    if (needs_sampler && info->sampler == VK_NULL_HANDLE)
        info->sampler = state->dummy_sampler;
    if (needs_view && info->imageView == VK_NULL_HANDLE) {
        info->imageView = state->dummy_image_view;
        info->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    }
}

static void sanitize_buffer_info(const struct device_state *state,
                                 VkDescriptorBufferInfo *info)
{
    if (info->buffer == VK_NULL_HANDLE) {
        info->buffer = state->dummy_buffer;
        info->offset = 0;
        info->range = DUMMY_BUFFER_SIZE;
    }
}

static int image_info_is_null(VkDescriptorType type,
                              const VkDescriptorImageInfo *info)
{
    if ((type == VK_DESCRIPTOR_TYPE_SAMPLER ||
         type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) &&
        info->sampler == VK_NULL_HANDLE)
        return 1;
    if (type != VK_DESCRIPTOR_TYPE_SAMPLER &&
        info->imageView == VK_NULL_HANDLE)
        return 1;
    return 0;
}

static int write_has_null_descriptor(const VkWriteDescriptorSet *write)
{
    enum descriptor_data_kind kind = descriptor_data_kind(write->descriptorType);
    if (!write->descriptorCount)
        return 0;
    if (kind == DESCRIPTOR_DATA_IMAGE && write->pImageInfo) {
        for (uint32_t i = 0; i < write->descriptorCount; ++i)
            if (image_info_is_null(write->descriptorType,
                                   &write->pImageInfo[i]))
                return 1;
    } else if (kind == DESCRIPTOR_DATA_BUFFER && write->pBufferInfo) {
        for (uint32_t i = 0; i < write->descriptorCount; ++i)
            if (write->pBufferInfo[i].buffer == VK_NULL_HANDLE)
                return 1;
    } else if (kind == DESCRIPTOR_DATA_VIEW && write->pTexelBufferView) {
        for (uint32_t i = 0; i < write->descriptorCount; ++i)
            if (write->pTexelBufferView[i] == VK_NULL_HANDLE)
                return 1;
    }
    return 0;
}

static void free_prepared_writes(
    VkWriteDescriptorSet *writes,
    uint32_t count)
{
    if (!writes)
        return;
    for (uint32_t i = 0; i < count; ++i) {
        enum descriptor_data_kind kind =
            descriptor_data_kind(writes[i].descriptorType);
        if (kind == DESCRIPTOR_DATA_IMAGE)
            free((void *)writes[i].pImageInfo);
        else if (kind == DESCRIPTOR_DATA_BUFFER)
            free((void *)writes[i].pBufferInfo);
        else if (kind == DESCRIPTOR_DATA_VIEW)
            free((void *)writes[i].pTexelBufferView);
    }
    free(writes);
}

static VkWriteDescriptorSet *prepare_writes(
    const struct device_state *state,
    uint32_t count,
    const VkWriteDescriptorSet *writes)
{
    int needs_copy = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (write_has_null_descriptor(&writes[i])) {
            needs_copy = 1;
            break;
        }
    }
    if (!needs_copy)
        return NULL;

    VkWriteDescriptorSet *copy =
        calloc(count, sizeof(*copy));
    if (!copy)
        return NULL;
    memcpy(copy, writes, (size_t)count * sizeof(*copy));

    for (uint32_t i = 0; i < count; ++i) {
        enum descriptor_data_kind kind =
            descriptor_data_kind(writes[i].descriptorType);
        uint32_t descriptor_count = writes[i].descriptorCount;
        if (!descriptor_count)
            continue;

        if (kind == DESCRIPTOR_DATA_IMAGE && writes[i].pImageInfo) {
            VkDescriptorImageInfo *infos =
                malloc((size_t)descriptor_count * sizeof(*infos));
            if (!infos)
                goto fail;
            memcpy(infos, writes[i].pImageInfo,
                   (size_t)descriptor_count * sizeof(*infos));
            for (uint32_t j = 0; j < descriptor_count; ++j)
                sanitize_image_info(state, writes[i].descriptorType,
                                    &infos[j]);
            copy[i].pImageInfo = infos;
        } else if (kind == DESCRIPTOR_DATA_BUFFER && writes[i].pBufferInfo) {
            VkDescriptorBufferInfo *infos =
                malloc((size_t)descriptor_count * sizeof(*infos));
            if (!infos)
                goto fail;
            memcpy(infos, writes[i].pBufferInfo,
                   (size_t)descriptor_count * sizeof(*infos));
            for (uint32_t j = 0; j < descriptor_count; ++j)
                sanitize_buffer_info(state, &infos[j]);
            copy[i].pBufferInfo = infos;
        } else if (kind == DESCRIPTOR_DATA_VIEW &&
                   writes[i].pTexelBufferView) {
            VkBufferView *views =
                malloc((size_t)descriptor_count * sizeof(*views));
            if (!views)
                goto fail;
            memcpy(views, writes[i].pTexelBufferView,
                   (size_t)descriptor_count * sizeof(*views));
            for (uint32_t j = 0; j < descriptor_count; ++j)
                if (views[j] == VK_NULL_HANDLE)
                    views[j] = state->dummy_buffer_view;
            copy[i].pTexelBufferView = views;
        }
    }
    return copy;

fail:
    free_prepared_writes(copy, count);
    return NULL;
}

static void VKAPI_CALL update_descriptor_sets(
    VkDevice device,
    uint32_t descriptor_write_count,
    const VkWriteDescriptorSet *descriptor_writes,
    uint32_t descriptor_copy_count,
    const VkCopyDescriptorSet *descriptor_copies)
{
    struct device_state state;
    if (!find_device(device, &state))
        return;
    PFN_vkUpdateDescriptorSets next = (PFN_vkUpdateDescriptorSets)
        device_proc(&state, "vkUpdateDescriptorSets");
    if (!next)
        return;

    VkWriteDescriptorSet *prepared = prepare_writes(
        &state, descriptor_write_count, descriptor_writes);
    next(device,
         descriptor_write_count,
         prepared ? prepared : descriptor_writes,
         descriptor_copy_count,
         descriptor_copies);
    free_prepared_writes(prepared, descriptor_write_count);
}

static void VKAPI_CALL cmd_push_descriptor_set_khr(
    VkCommandBuffer command_buffer,
    VkPipelineBindPoint pipeline_bind_point,
    VkPipelineLayout layout,
    uint32_t set,
    uint32_t descriptor_write_count,
    const VkWriteDescriptorSet *descriptor_writes)
{
    struct device_state state;
    if (!find_device(command_buffer, &state))
        return;
    PFN_vkCmdPushDescriptorSetKHR next =
        (PFN_vkCmdPushDescriptorSetKHR)
        device_proc(&state, "vkCmdPushDescriptorSetKHR");
    if (!next)
        return;

    VkWriteDescriptorSet *prepared = prepare_writes(
        &state, descriptor_write_count, descriptor_writes);
    next(command_buffer, pipeline_bind_point, layout, set,
         descriptor_write_count,
         prepared ? prepared : descriptor_writes);
    free_prepared_writes(prepared, descriptor_write_count);
}

static VkResult VKAPI_CALL create_update_template_common(
    VkDevice device,
    const VkDescriptorUpdateTemplateCreateInfo *info,
    const VkAllocationCallbacks *allocator,
    VkDescriptorUpdateTemplate *update_template,
    int khr)
{
    struct device_state state;
    if (!find_device(device, &state))
        return VK_ERROR_INITIALIZATION_FAILED;
    const char *name = khr
        ? "vkCreateDescriptorUpdateTemplateKHR"
        : "vkCreateDescriptorUpdateTemplate";
    PFN_vkCreateDescriptorUpdateTemplateKHR create =
        (PFN_vkCreateDescriptorUpdateTemplateKHR)device_proc(&state, name);
    if (!create)
        return VK_ERROR_EXTENSION_NOT_PRESENT;

    VkResult result = create(device, info, allocator, update_template);
    if (result != VK_SUCCESS)
        return result;

    struct template_state *template_state =
        calloc(1, sizeof(*template_state));
    if (!template_state) {
        const char *destroy_name = khr
            ? "vkDestroyDescriptorUpdateTemplateKHR"
            : "vkDestroyDescriptorUpdateTemplate";
        PFN_vkDestroyDescriptorUpdateTemplateKHR destroy =
            (PFN_vkDestroyDescriptorUpdateTemplateKHR)
            device_proc(&state, destroy_name);
        if (destroy)
            destroy(device, *update_template, allocator);
        *update_template = VK_NULL_HANDLE;
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    template_state->device = device;
    template_state->handle = *update_template;
    template_state->type = info->templateType;
    template_state->pipeline_bind_point = info->pipelineBindPoint;
    template_state->khr = khr;
    template_state->entry_count = info->descriptorUpdateEntryCount;
    if (template_state->entry_count) {
        template_state->entries =
            malloc((size_t)template_state->entry_count *
                   sizeof(*template_state->entries));
        if (!template_state->entries) {
            const char *destroy_name = khr
                ? "vkDestroyDescriptorUpdateTemplateKHR"
                : "vkDestroyDescriptorUpdateTemplate";
            PFN_vkDestroyDescriptorUpdateTemplateKHR destroy =
                (PFN_vkDestroyDescriptorUpdateTemplateKHR)
                device_proc(&state, destroy_name);
            if (destroy)
                destroy(device, *update_template, allocator);
            free(template_state);
            *update_template = VK_NULL_HANDLE;
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        memcpy(template_state->entries, info->pDescriptorUpdateEntries,
               (size_t)template_state->entry_count *
               sizeof(*template_state->entries));
    }
    pthread_mutex_lock(&state_lock);
    template_state->next = templates;
    templates = template_state;
    pthread_mutex_unlock(&state_lock);
    return VK_SUCCESS;
}

static VkResult VKAPI_CALL create_update_template(
    VkDevice device,
    const VkDescriptorUpdateTemplateCreateInfo *info,
    const VkAllocationCallbacks *allocator,
    VkDescriptorUpdateTemplate *update_template)
{
    return create_update_template_common(device, info, allocator,
                                         update_template, 0);
}

static VkResult VKAPI_CALL create_update_template_khr(
    VkDevice device,
    const VkDescriptorUpdateTemplateCreateInfo *info,
    const VkAllocationCallbacks *allocator,
    VkDescriptorUpdateTemplate *update_template)
{
    return create_update_template_common(device, info, allocator,
                                         update_template, 1);
}

static void VKAPI_CALL destroy_update_template_common(
    VkDevice device,
    VkDescriptorUpdateTemplate update_template,
    const VkAllocationCallbacks *allocator,
    int khr)
{
    struct device_state state;
    if (!find_device(device, &state))
        return;

    struct template_state retired;
    int found = 0;
    pthread_mutex_lock(&state_lock);
    for (struct template_state **slot = &templates; *slot; slot = &(*slot)->next) {
        if ((*slot)->device == device &&
            (*slot)->handle == update_template) {
            struct template_state *item = *slot;
            *slot = item->next;
            retired = *item;
            free(item);
            found = 1;
            break;
        }
    }
    pthread_mutex_unlock(&state_lock);

    const char *name = khr
        ? "vkDestroyDescriptorUpdateTemplateKHR"
        : "vkDestroyDescriptorUpdateTemplate";
    PFN_vkDestroyDescriptorUpdateTemplateKHR destroy =
        (PFN_vkDestroyDescriptorUpdateTemplateKHR)
        device_proc(&state, name);
    if (destroy)
        destroy(device, update_template, allocator);
    if (found)
        free(retired.entries);
}

static void VKAPI_CALL destroy_update_template(
    VkDevice device,
    VkDescriptorUpdateTemplate update_template,
    const VkAllocationCallbacks *allocator)
{
    destroy_update_template_common(device, update_template, allocator, 0);
}

static void VKAPI_CALL destroy_update_template_khr(
    VkDevice device,
    VkDescriptorUpdateTemplate update_template,
    const VkAllocationCallbacks *allocator)
{
    destroy_update_template_common(device, update_template, allocator, 1);
}

static int template_has_null_descriptor(
    const struct template_state *template_state,
    const void *data)
{
    const uint8_t *bytes = data;
    for (uint32_t i = 0; i < template_state->entry_count; ++i) {
        const VkDescriptorUpdateTemplateEntry *entry =
            &template_state->entries[i];
        enum descriptor_data_kind kind =
            descriptor_data_kind(entry->descriptorType);
        size_t element_size = descriptor_element_size(kind);
        if (!element_size || !entry->descriptorCount)
            continue;
        size_t stride = entry->stride ? entry->stride : element_size;
        const uint8_t *source = bytes + entry->offset;
        for (uint32_t j = 0; j < entry->descriptorCount; ++j) {
            const void *element = source + (size_t)j * stride;
            if (kind == DESCRIPTOR_DATA_IMAGE &&
                image_info_is_null(entry->descriptorType, element))
                return 1;
            if (kind == DESCRIPTOR_DATA_BUFFER &&
                ((const VkDescriptorBufferInfo *)element)->buffer ==
                    VK_NULL_HANDLE)
                return 1;
            if (kind == DESCRIPTOR_DATA_VIEW &&
                *(const VkBufferView *)element == VK_NULL_HANDLE)
                return 1;
        }
    }
    return 0;
}

static VkWriteDescriptorSet *expand_template(
    const struct device_state *state,
    const struct template_state *template_state,
    const void *data,
    uint32_t *descriptor_write_count)
{
    const uint8_t *bytes = data;
    uint32_t count = template_state->entry_count;
    VkWriteDescriptorSet *writes =
        calloc(count, sizeof(*writes));
    if (!writes)
        return NULL;

    for (uint32_t i = 0; i < count; ++i) {
        const VkDescriptorUpdateTemplateEntry *entry =
            &template_state->entries[i];
        enum descriptor_data_kind kind =
            descriptor_data_kind(entry->descriptorType);
        size_t element_size = descriptor_element_size(kind);
        size_t stride = entry->stride ? entry->stride : element_size;
        const uint8_t *source = bytes + entry->offset;

        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstBinding = entry->dstBinding;
        writes[i].dstArrayElement = entry->dstArrayElement;
        writes[i].descriptorType = entry->descriptorType;
        writes[i].descriptorCount = entry->descriptorCount;

        if (!entry->descriptorCount)
            continue;
        if (kind == DESCRIPTOR_DATA_IMAGE) {
            VkDescriptorImageInfo *infos =
                malloc((size_t)entry->descriptorCount * sizeof(*infos));
            if (!infos)
                goto fail;
            for (uint32_t j = 0; j < entry->descriptorCount; ++j) {
                memcpy(&infos[j], source + (size_t)j * stride,
                       sizeof(infos[j]));
                sanitize_image_info(state, entry->descriptorType,
                                    &infos[j]);
            }
            writes[i].pImageInfo = infos;
        } else if (kind == DESCRIPTOR_DATA_BUFFER) {
            VkDescriptorBufferInfo *infos =
                malloc((size_t)entry->descriptorCount * sizeof(*infos));
            if (!infos)
                goto fail;
            for (uint32_t j = 0; j < entry->descriptorCount; ++j) {
                memcpy(&infos[j], source + (size_t)j * stride,
                       sizeof(infos[j]));
                sanitize_buffer_info(state, &infos[j]);
            }
            writes[i].pBufferInfo = infos;
        } else if (kind == DESCRIPTOR_DATA_VIEW) {
            VkBufferView *views =
                malloc((size_t)entry->descriptorCount * sizeof(*views));
            if (!views)
                goto fail;
            for (uint32_t j = 0; j < entry->descriptorCount; ++j) {
                memcpy(&views[j], source + (size_t)j * stride,
                       sizeof(views[j]));
                if (views[j] == VK_NULL_HANDLE)
                    views[j] = state->dummy_buffer_view;
            }
            writes[i].pTexelBufferView = views;
        } else if (kind != DESCRIPTOR_DATA_NONE) {
            goto fail;
        }
    }
    *descriptor_write_count = count;
    return writes;

fail:
    free_prepared_writes(writes, count);
    return NULL;
}

static void update_template_common(
    VkDevice device,
    VkDescriptorSet descriptor_set,
    VkDescriptorUpdateTemplate update_template,
    const void *data,
    int khr)
{
    struct device_state state;
    struct template_state template_state;
    if (!find_device(device, &state))
        return;
    const char *update_name = khr
        ? "vkUpdateDescriptorSetWithTemplateKHR"
        : "vkUpdateDescriptorSetWithTemplate";
    PFN_vkUpdateDescriptorSetWithTemplate next_update_template =
        (PFN_vkUpdateDescriptorSetWithTemplate)
        device_proc(&state, update_name);
    if (!next_update_template)
        return;
    if (!data || !find_template(device, update_template, &template_state) ||
        !template_has_null_descriptor(&template_state, data)) {
        next_update_template(device, descriptor_set, update_template, data);
        return;
    }

    uint32_t write_count = 0;
    VkWriteDescriptorSet *writes = expand_template(
        &state, &template_state, data, &write_count);
    PFN_vkUpdateDescriptorSets next_update_sets =
        (PFN_vkUpdateDescriptorSets)
        device_proc(&state, "vkUpdateDescriptorSets");
    if (!writes || !next_update_sets) {
        free_prepared_writes(writes, write_count);
        next_update_template(device, descriptor_set, update_template, data);
        return;
    }
    for (uint32_t i = 0; i < write_count; ++i)
        writes[i].dstSet = descriptor_set;
    next_update_sets(device, write_count, writes, 0, NULL);
    free_prepared_writes(writes, write_count);
}

static void VKAPI_CALL update_descriptor_set_with_template(
    VkDevice device,
    VkDescriptorSet descriptor_set,
    VkDescriptorUpdateTemplate update_template,
    const void *data)
{
    update_template_common(device, descriptor_set, update_template, data, 0);
}

static void VKAPI_CALL update_descriptor_set_with_template_khr(
    VkDevice device,
    VkDescriptorSet descriptor_set,
    VkDescriptorUpdateTemplate update_template,
    const void *data)
{
    update_template_common(device, descriptor_set, update_template, data, 1);
}

static void VKAPI_CALL cmd_push_descriptor_set_with_template_khr(
    VkCommandBuffer command_buffer,
    VkDescriptorUpdateTemplate update_template,
    VkPipelineLayout layout,
    uint32_t set,
    const void *data)
{
    struct device_state state;
    struct template_state template_state;
    if (!find_device(command_buffer, &state))
        return;
    PFN_vkCmdPushDescriptorSetWithTemplateKHR next =
        (PFN_vkCmdPushDescriptorSetWithTemplateKHR)
        device_proc(&state, "vkCmdPushDescriptorSetWithTemplateKHR");
    if (!next)
        return;
    if (!data ||
        !find_template(state.handle, update_template, &template_state) ||
        !template_has_null_descriptor(&template_state, data)) {
        next(command_buffer, update_template, layout, set, data);
        return;
    }

    uint32_t write_count = 0;
    VkWriteDescriptorSet *writes = expand_template(
        &state, &template_state, data, &write_count);
    PFN_vkCmdPushDescriptorSetKHR next_push =
        (PFN_vkCmdPushDescriptorSetKHR)
        device_proc(&state, "vkCmdPushDescriptorSetKHR");
    if (!writes || !next_push) {
        free_prepared_writes(writes, write_count);
        next(command_buffer, update_template, layout, set, data);
        return;
    }
    next_push(command_buffer, template_state.pipeline_bind_point, layout, set,
              write_count, writes);
    free_prepared_writes(writes, write_count);
}

static PFN_vkVoidFunction get_physical_proc(
    VkInstance instance,
    const char *name)
{
    struct instance_state state;
    if (!instance || !name || !find_instance(instance, &state))
        return NULL;

    if (!strcmp(name, "vkEnumerateDeviceExtensionProperties"))
        return (PFN_vkVoidFunction)enumerate_device_extensions;
    if (!strcmp(name, "vkGetPhysicalDeviceFeatures2"))
        return (PFN_vkVoidFunction)get_physical_device_features2;
    if (!strcmp(name, "vkGetPhysicalDeviceFeatures2KHR"))
        return (PFN_vkVoidFunction)get_physical_device_features2_khr;

    return state.next_get_physical_device_proc_addr
        ? state.next_get_physical_device_proc_addr(instance, name)
        : NULL;
}

static PFN_vkVoidFunction VKAPI_CALL get_device_proc(
    VkDevice device,
    const char *name)
{
    struct device_state state;
    if (!device || !name || !find_device(device, &state))
        return NULL;
    if (!strcmp(name, "vkGetDeviceProcAddr"))
        return (PFN_vkVoidFunction)get_device_proc;
    if (!strcmp(name, "vkDestroyDevice"))
        return (PFN_vkVoidFunction)destroy_device;
    if (!strcmp(name, "vkUpdateDescriptorSets"))
        return (PFN_vkVoidFunction)update_descriptor_sets;
    if (!strcmp(name, "vkCreateDescriptorUpdateTemplate"))
        return (PFN_vkVoidFunction)create_update_template;
    if (!strcmp(name, "vkCreateDescriptorUpdateTemplateKHR"))
        return (PFN_vkVoidFunction)create_update_template_khr;
    if (!strcmp(name, "vkDestroyDescriptorUpdateTemplate"))
        return (PFN_vkVoidFunction)destroy_update_template;
    if (!strcmp(name, "vkDestroyDescriptorUpdateTemplateKHR"))
        return (PFN_vkVoidFunction)destroy_update_template_khr;
    if (!strcmp(name, "vkUpdateDescriptorSetWithTemplate"))
        return (PFN_vkVoidFunction)update_descriptor_set_with_template;
    if (!strcmp(name, "vkUpdateDescriptorSetWithTemplateKHR"))
        return (PFN_vkVoidFunction)update_descriptor_set_with_template_khr;
    if (!strcmp(name, "vkCmdPushDescriptorSetKHR"))
        return (PFN_vkVoidFunction)cmd_push_descriptor_set_khr;
    if (!strcmp(name, "vkCmdPushDescriptorSetWithTemplateKHR"))
        return (PFN_vkVoidFunction)cmd_push_descriptor_set_with_template_khr;
    return state.next_get_device_proc_addr(device, name);
}

static PFN_vkVoidFunction VKAPI_CALL get_instance_proc(
    VkInstance instance,
    const char *name)
{
    if (!name)
        return NULL;
    if (!strcmp(name, "vkGetInstanceProcAddr"))
        return (PFN_vkVoidFunction)get_instance_proc;
    if (!strcmp(name, "vkCreateInstance"))
        return (PFN_vkVoidFunction)create_instance;
    if (!strcmp(name, "vkGetDeviceProcAddr"))
        return (PFN_vkVoidFunction)get_device_proc;

    struct instance_state state;
    if (!instance || !find_instance(instance, &state))
        return NULL;
    if (!strcmp(name, "vkCreateDevice"))
        return (PFN_vkVoidFunction)create_device;
    if (!strcmp(name, "vkDestroyInstance"))
        return (PFN_vkVoidFunction)destroy_instance;

    PFN_vkVoidFunction intercepted = get_physical_proc(instance, name);
    if (intercepted)
        return intercepted;
    return state.next_get_instance_proc_addr(instance, name);
}

__attribute__((visibility("default"))) VkResult VKAPI_CALL
vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface *version)
{
    if (!version || version->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT ||
        version->loaderLayerInterfaceVersion < 2)
        return VK_ERROR_INITIALIZATION_FAILED;
    version->loaderLayerInterfaceVersion = 2;
    version->pfnGetInstanceProcAddr = get_instance_proc;
    version->pfnGetDeviceProcAddr = get_device_proc;
    version->pfnGetPhysicalDeviceProcAddr = get_physical_proc;
    return VK_SUCCESS;
}
