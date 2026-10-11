/* SPDX-License-Identifier: Apache-2.0 */
#define VK_NO_PROTOTYPES
#include "memory_visibility.h"
#include "../layer/layer.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define H(t,n) ((t)(uintptr_t)(n))
static uint64_t generation=1;
static VKAPI_ATTR void VKAPI_CALL memory_properties(VkPhysicalDevice p,VkPhysicalDeviceMemoryProperties *out)
{(void)p;memset(out,0,sizeof(*out));out->memoryTypeCount=1;out->memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;}
static VKAPI_ATTR void VKAPI_CALL properties(VkPhysicalDevice p,VkPhysicalDeviceProperties *out)
{(void)p;memset(out,0,sizeof(*out));out->limits.nonCoherentAtomSize=64;}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL instance_proc(VkInstance i,const char *n)
{(void)i;if(!strcmp(n,"vkGetPhysicalDeviceMemoryProperties")) return (PFN_vkVoidFunction)memory_properties;
 if(!strcmp(n,"vkGetPhysicalDeviceProperties")) return (PFN_vkVoidFunction)properties;
 assert(0);return NULL;}
int hybris_layer_lookup_device(VkDevice d,struct hybris_layer_device *out)
{*out=(struct hybris_layer_device){.handle=d,.physical=H(VkPhysicalDevice,1),.generation=generation};return 1;}
int hybris_layer_lookup_physical(VkPhysicalDevice p,struct hybris_layer_physical *out)
{(void)p;*out=(struct hybris_layer_physical){.instance=H(VkInstance,1),.resolver=instance_proc};return 1;}
int hybris_layer_command_device(VkCommandBuffer c,struct hybris_layer_device *d)
{(void)c;return hybris_layer_lookup_device(H(VkDevice,1),d);}
void hybris_layer_command_error(VkCommandBuffer c,VkResult e) {(void)c;(void)e;assert(0);}
PFN_vkVoidFunction hybris_readback_record_proc(const char *n) {(void)n;return NULL;}
PFN_vkVoidFunction hybris_readback_submit_proc(const char *n) {(void)n;return NULL;}
void hybris_readback_release_submissions(VkDevice d) {(void)d;}
static VKAPI_ATTR VkResult VKAPI_CALL allocate(VkDevice d,const VkMemoryAllocateInfo *i,const VkAllocationCallbacks *a,VkDeviceMemory *out)
{(void)d;(void)i;(void)a;*out=H(VkDeviceMemory,1);return VK_SUCCESS;}
static VKAPI_ATTR void VKAPI_CALL free_memory(VkDevice d,VkDeviceMemory m,const VkAllocationCallbacks *a)
{(void)d;(void)m;(void)a;}
static VKAPI_ATTR VkResult VKAPI_CALL create(VkDevice d,const VkBufferCreateInfo *i,const VkAllocationCallbacks *a,VkBuffer *out)
{(void)d;(void)i;(void)a;*out=H(VkBuffer,1);return VK_SUCCESS;}
static VKAPI_ATTR void VKAPI_CALL destroy(VkDevice d,VkBuffer b,const VkAllocationCallbacks *a)
{(void)d;(void)b;(void)a;}
static VKAPI_ATTR VkResult VKAPI_CALL bind(VkDevice d,VkBuffer b,VkDeviceMemory m,VkDeviceSize o)
{(void)d;(void)b;(void)m;(void)o;return VK_SUCCESS;}
PFN_vkVoidFunction hybris_layer_device_inner_proc(VkDevice d,const char *n)
{(void)d;
#define P(name,fn) if(!strcmp(n,"vk" #name)) return (PFN_vkVoidFunction)fn
 P(AllocateMemory,allocate);P(FreeMemory,free_memory);P(CreateBuffer,create);P(DestroyBuffer,destroy);P(BindBufferMemory,bind);
#undef P
 assert(0);return NULL;}
int main(void)
{
#define PROC(n) PFN_vk##n n=(PFN_vk##n)hybris_memory_visibility_proc("vk" #n)
 PROC(AllocateMemory);PROC(FreeMemory);PROC(CreateBuffer);PROC(DestroyBuffer);PROC(BindBufferMemory);
#undef PROC
 VkDevice device=H(VkDevice,1);struct hybris_layer_device owner;hybris_layer_lookup_device(device,&owner);
 VkMemoryAllocateInfo allocation={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=16};
 VkDeviceMemory memory;VkBuffer buffer;
 VkBufferCreateInfo info={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=8,
  .usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_VERTEX_BUFFER_BIT};
 assert(AllocateMemory(device,&allocation,NULL,&memory)==VK_SUCCESS);
 assert(CreateBuffer(device,&info,NULL,&buffer)==VK_SUCCESS);
 assert(BindBufferMemory(device,buffer,memory,4)==VK_SUCCESS);
 VkDescriptorBufferInfo out;
 assert(hybris_memory_visibility_storage_buffer(&owner,buffer,1,VK_WHOLE_SIZE,&out)==VK_SUCCESS);
 assert(out.buffer==buffer && out.offset==1 && out.range==7);
 assert(hybris_memory_visibility_storage_buffer(&owner,buffer,8,VK_WHOLE_SIZE,&out)==VK_ERROR_FEATURE_NOT_PRESENT && !out.buffer);
 assert(hybris_memory_visibility_storage_buffer(&owner,buffer,1,8,&out)==VK_ERROR_FEATURE_NOT_PRESENT);
 assert(hybris_memory_visibility_storage_buffer(&owner,buffer,0,0,&out)==VK_ERROR_FEATURE_NOT_PRESENT);
 assert(hybris_memory_visibility_storage_buffer(&owner,buffer,UINT64_MAX,1,&out)==VK_ERROR_FEATURE_NOT_PRESENT);
 ++generation;hybris_layer_lookup_device(device,&owner);
 assert(hybris_memory_visibility_storage_buffer(&owner,buffer,0,4,&out)==VK_ERROR_FEATURE_NOT_PRESENT);
 --generation;hybris_layer_lookup_device(device,&owner);
 FreeMemory(device,memory,NULL);
 assert(hybris_memory_visibility_storage_buffer(&owner,buffer,0,4,&out)==VK_ERROR_FEATURE_NOT_PRESENT);
 assert(AllocateMemory(device,&allocation,NULL,&memory)==VK_SUCCESS);
 assert(hybris_memory_visibility_storage_buffer(&owner,buffer,0,4,&out)==VK_ERROR_FEATURE_NOT_PRESENT);
 assert(BindBufferMemory(device,buffer,memory,4)==VK_SUCCESS);
 assert(hybris_memory_visibility_storage_buffer(&owner,buffer,0,4,&out)==VK_SUCCESS);
 assert(BindBufferMemory(device,buffer,memory,12)==VK_SUCCESS);
 assert(hybris_memory_visibility_storage_buffer(&owner,buffer,0,4,&out)==VK_ERROR_FEATURE_NOT_PRESENT);
 DestroyBuffer(device,buffer,NULL);info.usage=VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
 assert(CreateBuffer(device,&info,NULL,&buffer)==VK_SUCCESS);assert(BindBufferMemory(device,buffer,memory,0)==VK_SUCCESS);
 assert(hybris_memory_visibility_storage_buffer(&owner,buffer,0,4,&out)==VK_ERROR_FEATURE_NOT_PRESENT);
 DestroyBuffer(device,buffer,NULL);FreeMemory(device,memory,NULL);hybris_memory_visibility_release_device(device);
 puts("PASS storage buffer metadata: bounded ranges, usage, allocation/device generations, freed/reused handles, no staging/readback");
}
