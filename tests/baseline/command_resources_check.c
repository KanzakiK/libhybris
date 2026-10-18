/* SPDX-License-Identifier: Apache-2.0 */
#define VK_NO_PROTOTYPES
#include "../layer/layer.h"
#include "polygon_buffers.h"
#include "polygon_raster.h"
#include "bc_context.h"
#include "memory_visibility.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#define H(t,n) ((t)(uintptr_t)(n))
static unsigned next_pool=1,next_command=1,destroyed;
static VkResult native_result=VK_SUCCESS;
/* This test isolates command ownership. GPU conversion is covered by the
 * polygon probe; no conversion resources may be created in this mock device. */
int hybris_shader_polygon_active(VkDevice d) {(void)d;return 0;}
PFN_vkVoidFunction hybris_memory_visibility_proc(const char *n) {(void)n;return NULL;}
struct hybris_bc_command *hybris_bc_command_find(VkCommandBuffer c,struct hybris_bc_device **d)
{(void)c;*d=NULL;return NULL;}
struct hybris_polygon_pipeline *hybris_polygon_pipeline_acquire(VkDevice d,VkPipeline p)
{(void)d;(void)p;return NULL;}
void hybris_polygon_pipeline_release(void *p) {(void)p;assert(0);}
void hybris_polygon_buffers_destroy(void *p) {(void)p;assert(0);}
void hybris_polygon_bindings_destroy(struct hybris_polygon_bindings *p) {(void)p;assert(0);}
void hybris_bc_restore_compute(struct hybris_bc_command_state *s,VkCommandBuffer c)
{(void)s;(void)c;assert(0);}
void hybris_bc_restore_graphics(struct hybris_bc_command_state *s,VkCommandBuffer c)
{(void)s;(void)c;assert(0);}
VkResult hybris_memory_visibility_storage_buffer(const struct hybris_layer_device *d,VkBuffer b,VkDeviceSize o,VkDeviceSize n,VkDescriptorBufferInfo *out)
{(void)d;(void)b;(void)o;(void)n;(void)out;assert(0);return VK_ERROR_UNKNOWN;}
VkResult hybris_polygon_indices_create(VkDevice d,PFN_vkGetDeviceProcAddr f,const VkAllocationCallbacks *a,struct hybris_polygon_indices *out)
{(void)d;(void)f;(void)a;(void)out;assert(0);return VK_ERROR_UNKNOWN;}
void hybris_polygon_indices_destroy(struct hybris_polygon_indices *k,const VkAllocationCallbacks *a)
{(void)k;(void)a;assert(0);}
VkResult hybris_polygon_bindings_prepare(const struct hybris_polygon_pipeline *p,const VkDescriptorSetLayoutCreateInfo *s,
 const struct hybris_bc_layout *i,const struct hybris_bc_command_state *j,const VkAllocationCallbacks *a,struct hybris_polygon_bindings **out)
{(void)p;(void)s;(void)i;(void)j;(void)a;(void)out;assert(0);return VK_ERROR_UNKNOWN;}
VkResult hybris_polygon_buffers_create(const struct hybris_polygon_pipeline *p,const struct hybris_polygon_indices *k,
 const VkPhysicalDeviceMemoryProperties *m,const VkPhysicalDeviceLimits *l,const struct hybris_polygon_draw *d,
 const VkDescriptorBufferInfo *s,const VkDescriptorBufferInfo *i,const VkAllocationCallbacks *a,struct hybris_polygon_buffers **out)
{(void)p;(void)k;(void)m;(void)l;(void)d;(void)s;(void)i;(void)a;(void)out;assert(0);return VK_ERROR_UNKNOWN;}
VkResult hybris_polygon_buffers_compute(const struct hybris_polygon_buffers *b,const struct hybris_polygon_pipeline *p,
 const struct hybris_polygon_indices *k,const struct hybris_polygon_bindings *s,VkCommandBuffer c)
{(void)b;(void)p;(void)k;(void)s;(void)c;assert(0);return VK_ERROR_UNKNOWN;}
VkResult hybris_polygon_raster_record(const struct hybris_polygon_pipeline *p,const struct hybris_polygon_bindings *b,
 struct hybris_bc_command_state *j,VkCommandBuffer c,VkPolygonMode m,VkDescriptorSet s,VkBuffer g,VkBuffer i,
 const struct hybris_polygon_source *v)
{(void)p;(void)b;(void)j;(void)c;(void)m;(void)s;(void)g;(void)i;(void)v;assert(0);return VK_ERROR_UNKNOWN;}
static unsigned recorded_draws;
static void VKAPI_CALL record_draw(VkCommandBuffer c,uint32_t n,uint32_t instances,uint32_t first,uint32_t base)
{ assert(c && n==3 && instances==2 && first==1 && base==4);++recorded_draws; }
static void VKAPI_CALL record_indexed(VkCommandBuffer c,uint32_t n,uint32_t instances,uint32_t first,int32_t offset,uint32_t base)
{ assert(c && n==3 && instances==2 && first==1 && offset==-2 && base==4);++recorded_draws; }
static const struct hybris_layer_command_dispatch recording={.CmdDraw=record_draw,.CmdDrawIndexed=record_indexed};
int hybris_layer_lookup_device(VkDevice d,struct hybris_layer_device *out)
{ *out=(struct hybris_layer_device){.handle=d,.generation=1,.recording=&recording};return 1; }
void hybris_readback_reset_command(VkCommandBuffer c) {(void)c;}
void hybris_readback_reset_pool(VkDevice d,VkCommandPool p) {(void)d;(void)p;}
static VKAPI_ATTR VkResult VKAPI_CALL create_pool(VkDevice d,const VkCommandPoolCreateInfo *i,
 const VkAllocationCallbacks *a,VkCommandPool *out)
{(void)d;(void)i;(void)a;*out=H(VkCommandPool,next_pool++);return VK_SUCCESS;}
static VKAPI_ATTR VkResult VKAPI_CALL allocate(VkDevice d,const VkCommandBufferAllocateInfo *i,VkCommandBuffer *out)
{(void)d;for(uint32_t j=0;j<i->commandBufferCount;++j) out[j]=H(VkCommandBuffer,next_command++);return VK_SUCCESS;}
static VKAPI_ATTR VkResult VKAPI_CALL begin(VkCommandBuffer c,const VkCommandBufferBeginInfo *i)
{(void)c;(void)i;return native_result;}
static VKAPI_ATTR VkResult VKAPI_CALL end(VkCommandBuffer c) {(void)c;return native_result;}
static VKAPI_ATTR VkResult VKAPI_CALL reset(VkCommandBuffer c,VkCommandBufferResetFlags f)
{(void)c;(void)f;return native_result;}
static VKAPI_ATTR VkResult VKAPI_CALL reset_pool(VkDevice d,VkCommandPool p,VkCommandPoolResetFlags f)
{(void)d;(void)p;(void)f;return native_result;}
static VKAPI_ATTR void VKAPI_CALL free_commands(VkDevice d,VkCommandPool p,uint32_t n,const VkCommandBuffer *c)
{(void)d;(void)p;(void)n;(void)c;}
static VKAPI_ATTR void VKAPI_CALL destroy_pool(VkDevice d,VkCommandPool p,const VkAllocationCallbacks *a)
{(void)d;(void)p;(void)a;}
PFN_vkVoidFunction hybris_layer_device_inner_proc(VkDevice d,const char *name)
{
 (void)d;
#define P(n,f) if(!strcmp(name,"vk" #n)) return (PFN_vkVoidFunction)f
 P(CreateCommandPool,create_pool);P(AllocateCommandBuffers,allocate);P(BeginCommandBuffer,begin);
 P(EndCommandBuffer,end);P(ResetCommandBuffer,reset);P(ResetCommandPool,reset_pool);
 P(FreeCommandBuffers,free_commands);P(DestroyCommandPool,destroy_pool);
#undef P
 assert(0);return NULL;
}
static void destroy_resource(void *resource)
{
 /* Re-entering a read-only registry operation proves callbacks are outside
  * its lock. The owning command is allowed to be detached already. */
 struct hybris_layer_device device;
 (void)hybris_layer_command_device(H(VkCommandBuffer,1),&device);
 assert(resource);++destroyed;
}
int main(void)
{
#define PROC(n) PFN_vk##n n=(PFN_vk##n)hybris_layer_commands_proc("vk" #n)
 PROC(CreateCommandPool);PROC(AllocateCommandBuffers);PROC(BeginCommandBuffer);
 PROC(EndCommandBuffer);PROC(ResetCommandBuffer);PROC(ResetCommandPool);
 PROC(FreeCommandBuffers);PROC(DestroyCommandPool);PROC(CmdDraw);PROC(CmdDrawIndexed);
#undef PROC
 VkDevice device=H(VkDevice,1);VkCommandPool a,b;
 VkCommandPoolCreateInfo pool={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
 assert(CreateCommandPool(device,&pool,NULL,&a)==VK_SUCCESS);
 assert(CreateCommandPool(device,&pool,NULL,&b)==VK_SUCCESS);
 VkCommandBuffer commands[3];
 VkCommandBufferAllocateInfo info={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
  .commandPool=a,.commandBufferCount=2,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY};
 assert(AllocateCommandBuffers(device,&info,commands)==VK_SUCCESS);
 info.commandPool=b;info.commandBufferCount=1;
 assert(AllocateCommandBuffers(device,&info,commands+2)==VK_SUCCESS);
 /* Recording dispatch is already resolved. Any name lookup here reaches
  * the mock resolver's assertion rather than silently hiding hot-path work. */
 for(unsigned i=0;i<32;++i) {
  CmdDraw(commands[0],3,2,1,4);CmdDrawIndexed(commands[0],3,2,1,-2,4);
 }
 assert(recorded_draws==64);
 for(unsigned i=0;i<3;++i) assert(hybris_layer_command_retain(commands[i],(void*)(uintptr_t)(i+1),destroy_resource)==VK_SUCCESS);
 assert(EndCommandBuffer(commands[0])==VK_SUCCESS && destroyed==0);
 VkCommandBufferBeginInfo cb={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
 native_result=VK_ERROR_OUT_OF_HOST_MEMORY;
 assert(BeginCommandBuffer(commands[0],&cb)==native_result && destroyed==0);
 assert(ResetCommandPool(device,a,0)==native_result && destroyed==0);
 assert(ResetCommandBuffer(commands[0],0)==native_result && destroyed==0);
 native_result=VK_SUCCESS;
 assert(BeginCommandBuffer(commands[0],&cb)==VK_SUCCESS && destroyed==1);
 assert(hybris_layer_command_retain(commands[0],(void*)1,destroy_resource)==VK_SUCCESS);
 assert(ResetCommandPool(device,a,0)==VK_SUCCESS && destroyed==3);
 assert(ResetCommandPool(device,a,0)==VK_SUCCESS && destroyed==3);
 assert(ResetCommandBuffer(commands[2],0)==VK_SUCCESS && destroyed==4);
 assert(hybris_layer_command_retain(commands[0],(void*)1,destroy_resource)==VK_SUCCESS);
 assert(hybris_layer_command_retain(commands[1],(void*)2,destroy_resource)==VK_SUCCESS);
 assert(hybris_layer_command_retain(commands[2],(void*)3,destroy_resource)==VK_SUCCESS);
 FreeCommandBuffers(device,a,1,commands);assert(destroyed==5);
 assert(hybris_layer_command_retain(commands[0],(void*)1,destroy_resource)==VK_ERROR_INITIALIZATION_FAILED && destroyed==5);
 DestroyCommandPool(device,a,NULL);assert(destroyed==6);
 hybris_layer_commands_release_device(device);assert(destroyed==7);
 puts("PASS command resources: retained across end/failure, begin/reset/free/device retirement, pool isolation, no double-free, callbacks outside locks");
 /* Exercise indexed hot-path lookups and retirement with real wrapper
  * allocations, including collision chains and driver-handle reuse. */
 const unsigned sizes[]={16,152,2048};
 for(unsigned run=0;run<3;++run) {
  unsigned count=sizes[run];VkCommandBuffer many[2048];
  assert(CreateCommandPool(device,&pool,NULL,&a)==VK_SUCCESS);
  info.commandPool=a;info.commandBufferCount=count;
  assert(AllocateCommandBuffers(device,&info,many)==VK_SUCCESS);
  struct hybris_layer_device owner;
  for(unsigned i=0;i<count;++i) assert(hybris_layer_command_device(many[i],&owner) && owner.handle==device);
  struct timespec start,finish;clock_gettime(CLOCK_MONOTONIC,&start);
  for(unsigned i=0;i<100000;++i) assert(hybris_layer_command_device(many[0],&owner));
  clock_gettime(CLOCK_MONOTONIC,&finish);
  double ns=(finish.tv_sec-start.tv_sec)*1e9+finish.tv_nsec-start.tv_nsec;
  printf("layer command lookup live=%u ns/call=%.2f\n",count,ns/100000);
  assert(ResetCommandPool(device,a,0)==VK_SUCCESS);
  for(unsigned i=0;i<count;++i) assert(hybris_layer_command_device(many[i],&owner));
  FreeCommandBuffers(device,a,count/2,many);
  for(unsigned i=0;i<count;++i) assert(!!hybris_layer_command_device(many[i],&owner)==(i>=count/2));
  DestroyCommandPool(device,a,NULL);
  for(unsigned i=0;i<count;++i) assert(!hybris_layer_command_device(many[i],&owner));
  next_command=(uintptr_t)many[0];
  assert(CreateCommandPool(device,&pool,NULL,&b)==VK_SUCCESS);
  info.commandPool=b;info.commandBufferCount=1;
  assert(AllocateCommandBuffers(device,&info,commands)==VK_SUCCESS && commands[0]==many[0]);
  assert(hybris_layer_command_device(commands[0],&owner));
  hybris_layer_commands_release_device(device);
  assert(!hybris_layer_command_device(commands[0],&owner));
 }
 puts("PASS indexed layer command scale/reset/free/pool/device retirement/handle reuse");
}
