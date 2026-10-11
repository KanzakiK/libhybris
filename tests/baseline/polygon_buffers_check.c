/* SPDX-License-Identifier: Apache-2.0 */
#define VK_NO_PROTOTYPES
#include "polygon_buffers.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define H(t,n) ((t)(uintptr_t)(n))
static unsigned calls,fail_at,created_buffers,live_buffers,live_memory,live_pools;
static uint32_t mapped[5][128];
static VkResult result(void) {return ++calls==fail_at ? VK_ERROR_OUT_OF_DEVICE_MEMORY : VK_SUCCESS;}
static VKAPI_ATTR VkResult VKAPI_CALL create_buffer(VkDevice d,const VkBufferCreateInfo *i,
 const VkAllocationCallbacks *a,VkBuffer *out)
{(void)d;(void)a;assert(i->size && (i->usage&VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
 VkResult r=result();if(!r){*out=H(VkBuffer,++created_buffers);++live_buffers;}return r;}
static VKAPI_ATTR void VKAPI_CALL requirements(VkDevice d,VkBuffer b,VkMemoryRequirements *r)
{(void)d;(void)b;*r=(VkMemoryRequirements){.size=512,.alignment=16,.memoryTypeBits=1};}
static VKAPI_ATTR VkResult VKAPI_CALL allocate(VkDevice d,const VkMemoryAllocateInfo *i,
 const VkAllocationCallbacks *a,VkDeviceMemory *out)
{(void)d;(void)a;assert(i->allocationSize==512 && !i->memoryTypeIndex);
 VkResult r=result();if(!r){*out=H(VkDeviceMemory,created_buffers);++live_memory;}return r;}
static VKAPI_ATTR VkResult VKAPI_CALL bind(VkDevice d,VkBuffer b,VkDeviceMemory m,VkDeviceSize o)
{(void)d;assert((uintptr_t)b==(uintptr_t)m && !o);return result();}
static VKAPI_ATTR VkResult VKAPI_CALL map(VkDevice d,VkDeviceMemory m,VkDeviceSize o,VkDeviceSize s,
 VkMemoryMapFlags f,void **out)
{(void)d;assert(!o && !f && s<=512 && ((uintptr_t)m==1 || (uintptr_t)m==3));
 VkResult r=result();if(!r)*out=mapped[(uintptr_t)m-1];return r;}
static VKAPI_ATTR void VKAPI_CALL unmap(VkDevice d,VkDeviceMemory m){(void)d;(void)m;}
static VKAPI_ATTR VkResult VKAPI_CALL pool(VkDevice d,const VkDescriptorPoolCreateInfo *i,
 const VkAllocationCallbacks *a,VkDescriptorPool *out)
{(void)d;(void)a;assert(i->maxSets==2 && i->poolSizeCount==1 && i->pPoolSizes[0].descriptorCount==8);
 VkResult r=result();if(!r){*out=H(VkDescriptorPool,1);++live_pools;}return r;}
static VKAPI_ATTR VkResult VKAPI_CALL sets(VkDevice d,const VkDescriptorSetAllocateInfo *i,VkDescriptorSet *s)
{(void)d;assert(i->descriptorSetCount==2 && i->descriptorPool && i->pSetLayouts[0]==H(VkDescriptorSetLayout,1));
 VkResult r=result();if(!r){s[0]=H(VkDescriptorSet,1);s[1]=H(VkDescriptorSet,2);}return r;}
static VKAPI_ATTR void VKAPI_CALL update(VkDevice d,uint32_t n,const VkWriteDescriptorSet *w,
 uint32_t nc,const VkCopyDescriptorSet *c)
{(void)d;(void)c;assert(n==8 && !nc);
 for(unsigned i=0;i<n;++i) assert(w[i].pBufferInfo->range && w[i].descriptorCount==1);
 assert(w[4].pBufferInfo->buffer==H(VkBuffer,2) && w[7].pBufferInfo->buffer==H(VkBuffer,5));}
static VKAPI_ATTR void VKAPI_CALL destroy_buffer(VkDevice d,VkBuffer b,const VkAllocationCallbacks *a)
{(void)d;(void)b;(void)a;assert(live_buffers);--live_buffers;}
static VKAPI_ATTR void VKAPI_CALL free_memory(VkDevice d,VkDeviceMemory m,const VkAllocationCallbacks *a)
{(void)d;(void)m;(void)a;assert(live_memory);--live_memory;}
static VKAPI_ATTR void VKAPI_CALL destroy_pool(VkDevice d,VkDescriptorPool p,const VkAllocationCallbacks *a)
{(void)d;(void)p;(void)a;assert(live_pools);--live_pools;}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL resolve(VkDevice d,const char *name)
{(void)d;
#define P(n,f) if(!strcmp(name,"vk" #n)) return (PFN_vkVoidFunction)f
 P(CreateBuffer,create_buffer);P(GetBufferMemoryRequirements,requirements);P(AllocateMemory,allocate);
 P(BindBufferMemory,bind);P(MapMemory,map);P(UnmapMemory,unmap);P(CreateDescriptorPool,pool);
 P(AllocateDescriptorSets,sets);P(UpdateDescriptorSets,update);P(DestroyBuffer,destroy_buffer);
 P(FreeMemory,free_memory);P(DestroyDescriptorPool,destroy_pool);
#undef P
 assert(0);return NULL;}
/* Recording is not exercised by this allocation/bounds test. */
VkResult hybris_polygon_bindings_bind(const struct hybris_polygon_bindings *b,VkCommandBuffer c,VkPipelineBindPoint p)
{(void)b;(void)c;(void)p;assert(0);return VK_ERROR_UNKNOWN;}
VkResult hybris_polygon_indices_record(const struct hybris_polygon_indices *k,VkCommandBuffer c,
 VkDescriptorSet s,const struct hybris_polygon_push *p)
{(void)k;(void)c;(void)s;(void)p;assert(0);return VK_ERROR_UNKNOWN;}
int main(void)
{
 VkDescriptorSetLayout layout=H(VkDescriptorSetLayout,1);
 struct hybris_polygon_pipeline p={.device=H(VkDevice,1),.resolver=resolve,.sets=&layout,
   .records={.input_count=1,.output_stride=48}};
 p.records.inputs[0].components=4;p.bindings[0].stride=16;
 struct hybris_polygon_indices k={.device=p.device,.descriptors=H(VkDescriptorSetLayout,2)};
 VkPhysicalDeviceMemoryProperties memory={.memoryTypeCount=1};
 memory.memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT|VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
 VkPhysicalDeviceLimits limits={.maxStorageBufferRange=4096,.minStorageBufferOffsetAlignment=16,
   .maxComputeWorkGroupCount={1024,1,1},.maxComputeWorkGroupSize={1,1,1},.maxComputeWorkGroupInvocations=1};
 struct hybris_polygon_draw d={.count=3,.instances=2,.first=1,.first_instance=5,
   .mode=VK_POLYGON_MODE_LINE,.index_capacity=2};
 VkDescriptorBufferInfo input={H(VkBuffer,99),0,256},index={H(VkBuffer,98),0,16};
 struct hybris_polygon_buffers *b=NULL;
#define CREATE() hybris_polygon_buffers_create(&p,&k,&memory,&limits,&d,&input,&index,NULL,&b)
 assert(CREATE()==VK_SUCCESS && b && b->invocations==6 && b->sizes[1]==288 && b->push.capacity==2);
 assert(b->push.filled.indexCount==3 && b->push.filled.instanceCount==2 && b->push.filled.firstIndex==1);
 assert(b->push.filled.vertexOffset==5 && !b->push.filled.firstInstance);
 assert(mapped[0][0]==1 && mapped[0][3]==5 && mapped[0][4]==3 && !mapped[0][5]);
 for(unsigned i=0;i<6;++i) assert(mapped[2][i]==i);
 hybris_polygon_buffers_destroy(b);assert(!live_buffers && !live_memory && !live_pools);
 unsigned success_calls=calls;
 for(unsigned f=1;f<=success_calls;++f) {
   calls=created_buffers=0;fail_at=f;
   assert(CREATE()==VK_ERROR_OUT_OF_DEVICE_MEMORY && !b && !live_buffers && !live_memory && !live_pools);
 }
 calls=created_buffers=fail_at=0;
 d.indexed=VK_TRUE;d.index_type=VK_INDEX_TYPE_UINT16;d.vertex_offset=-2;
 assert(CREATE()==VK_SUCCESS && b->push.filled.vertexOffset==-2 && b->push.filled.firstInstance==5);
 assert(mapped[0][1]==1 && mapped[0][2]==(uint32_t)-2 && mapped[0][5]==2);
 hybris_polygon_buffers_destroy(b);calls=0;
 d.count=4;assert(CREATE()==VK_ERROR_FEATURE_NOT_PRESENT && !b && !calls);d.count=3;
 d.instances=UINT32_MAX;assert(CREATE()==VK_ERROR_FEATURE_NOT_PRESENT && !calls);d.instances=2;
 index.range=4;assert(CREATE()==VK_ERROR_FEATURE_NOT_PRESENT && !calls);index.range=16;
 input.offset=4;assert(CREATE()==VK_ERROR_FEATURE_NOT_PRESENT && !calls);input.offset=0;
 limits.maxStorageBufferRange=128;assert(CREATE()==VK_ERROR_FEATURE_NOT_PRESENT && !calls);limits.maxStorageBufferRange=4096;
 d.indexed=VK_FALSE;d.first=UINT32_MAX;assert(CREATE()==VK_ERROR_FEATURE_NOT_PRESENT && !calls);d.first=1;
 p.bindings[0].inputRate=VK_VERTEX_INPUT_RATE_INSTANCE;input.range=64;
 assert(CREATE()==VK_ERROR_FEATURE_NOT_PRESENT && !calls);
 puts("PASS polygon buffers: exact draw metadata, overcapacity admitted for GPU fallback, bounds before allocation, partial-failure cleanup, only metadata mapped");
}
