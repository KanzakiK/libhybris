#define VK_NO_PROTOTYPES
#include "../../hybris/vulkan/compat/bc_context.c"
#include "../../hybris/vulkan/compat/bc_commands.c"
#include <assert.h>
#include <stdio.h>
static uintptr_t next_handle=1;
static VkResult native_result=VK_SUCCESS;
static unsigned resolves, recorded, second_recorded;
static void VKAPI_CALL record_pipeline(VkCommandBuffer c,VkPipelineBindPoint p,VkPipeline v)
{(void)c;(void)p;(void)v;++recorded;}
static void VKAPI_CALL record_pipeline_second(VkCommandBuffer c,VkPipelineBindPoint p,VkPipeline v)
{(void)c;(void)p;(void)v;++second_recorded;}
static void VKAPI_CALL record_descriptors(VkCommandBuffer c,VkPipelineBindPoint p,VkPipelineLayout l,
 uint32_t f,uint32_t n,const VkDescriptorSet *s,uint32_t o,const uint32_t *v)
{(void)c;(void)p;(void)l;(void)f;(void)n;(void)s;(void)o;(void)v;++recorded;}
static void VKAPI_CALL record_push(VkCommandBuffer c,VkPipelineLayout l,VkShaderStageFlags s,
 uint32_t o,uint32_t n,const void *v)
{(void)c;(void)l;(void)s;(void)o;(void)n;(void)v;++recorded;}
int hybris_shader_polygon_active(VkDevice d) {(void)d;return 0;}
void hybris_bc_transfer_init(struct hybris_bc_transfer *t,const struct hybris_bc_decoder *d,
 const VkPhysicalDeviceMemoryProperties *m,const VkPhysicalDeviceLimits *l)
{ memset(t,0,sizeof(*t));t->decoder=d;t->memory=m;t->limits=l; }
void hybris_bc_transfer_finish(struct hybris_bc_transfer *t) {(void)t;assert(0);}
void hybris_polygon_indices_destroy(struct hybris_polygon_indices *p,const VkAllocationCallbacks *a)
{(void)p;(void)a;assert(0);}
void hybris_bc_decoder_destroy(struct hybris_bc_decoder *d,const VkAllocationCallbacks *a)
{(void)d;(void)a;assert(0);}
void hybris_bc_image_destroy(struct hybris_bc_image *i,const VkAllocationCallbacks *a)
{(void)i;(void)a;assert(0);}
static VkResult VKAPI_CALL alloc(VkDevice d,const VkCommandBufferAllocateInfo *i,VkCommandBuffer *h)
{(void)d;if(native_result)return native_result;for(unsigned n=0;n<i->commandBufferCount;++n)h[n]=(VkCommandBuffer)(next_handle++*256);return VK_SUCCESS;}
static VkResult VKAPI_CALL reset(VkDevice d,VkCommandPool p,VkCommandPoolResetFlags f)
{(void)d;(void)p;(void)f;return native_result;}
static void VKAPI_CALL release(VkDevice d,VkCommandPool p,uint32_t n,const VkCommandBuffer *h)
{(void)d;(void)p;(void)n;(void)h;}
static void VKAPI_CALL destroy(VkDevice d,VkCommandPool p,const VkAllocationCallbacks *a)
{(void)d;(void)p;(void)a;}
static PFN_vkVoidFunction VKAPI_CALL resolve(VkDevice d,const char *n)
{++resolves;
 if(!strcmp(n,"vkCmdBindPipeline"))return (PFN_vkVoidFunction)(d==(VkDevice)2 ? record_pipeline_second : record_pipeline);
 if(!strcmp(n,"vkCmdBindDescriptorSets"))return (PFN_vkVoidFunction)record_descriptors;
 if(!strcmp(n,"vkCmdPushConstants"))return (PFN_vkVoidFunction)record_push;
 if(!strcmp(n,"vkAllocateCommandBuffers"))return (PFN_vkVoidFunction)alloc;
 if(!strcmp(n,"vkResetCommandPool"))return (PFN_vkVoidFunction)reset;
 if(!strcmp(n,"vkFreeCommandBuffers"))return (PFN_vkVoidFunction)release;
 if(!strcmp(n,"vkDestroyCommandPool"))return (PFN_vkVoidFunction)destroy;
 assert(0);return NULL;
}
int main(void)
{
 struct hybris_bc_device *d=calloc(1,sizeof(*d));assert(d);
 d->handle=(VkDevice)1;d->resolver=resolve;pthread_mutex_init(&d->guard,NULL);
 init_recording_dispatch(d);
 hybris_bc_layouts_init(&d->layouts,d->handle,resolve);devices=d;
 VkCommandBuffer handles[2048],other;
 VkCommandBufferAllocateInfo info={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
  .commandPool=(VkCommandPool)1,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=2048};
 assert(!allocate_commands(d->handle,&info,handles));
 info.commandPool=(VkCommandPool)2;info.commandBufferCount=1;
 assert(!allocate_commands(d->handle,&info,&other));
 unsigned before=resolves;
 for(unsigned i=0;i<10000;++i) {
  bind_pipeline(other,VK_PIPELINE_BIND_POINT_GRAPHICS,(VkPipeline)1);
  bind_descriptors(other,VK_PIPELINE_BIND_POINT_GRAPHICS,(VkPipelineLayout)1,0,0,NULL,0,NULL);
  push_constants(other,(VkPipelineLayout)1,VK_SHADER_STAGE_VERTEX_BIT,0,0,NULL);
 }
 assert(resolves==before && recorded==30000);
 struct hybris_bc_device second={.handle=(VkDevice)2,.resolver=resolve};
 init_recording_dispatch(&second);
 assert(second.bind_pipeline!=d->bind_pipeline);
 second.bind_pipeline(other,VK_PIPELINE_BIND_POINT_GRAPHICS,(VkPipeline)1);
 d->bind_pipeline(other,VK_PIPELINE_BIND_POINT_GRAPHICS,(VkPipeline)1);
 assert(second_recorded==1 && recorded==30001);
 puts("PASS immutable BC recording dispatch: no hot-path resolves and device isolation");
 struct hybris_bc_device *owner;
 native_result=VK_ERROR_OUT_OF_HOST_MEMORY;
 assert(reset_pool(d->handle,(VkCommandPool)1,0)==native_result);
 for(unsigned i=0;i<2048;++i)assert(hybris_bc_command_find(handles[i],&owner) && owner==d);
 native_result=VK_SUCCESS;assert(!reset_pool(d->handle,(VkCommandPool)1,0));
 for(unsigned i=0;i<2048;++i)assert(hybris_bc_command_find(handles[i],&owner) && owner==d);
 free_commands(d->handle,(VkCommandPool)1,1024,handles);
 for(unsigned i=0;i<1024;++i)assert(!hybris_bc_command_find(handles[i],&owner));
 destroy_pool(d->handle,(VkCommandPool)1,NULL);
 for(unsigned i=0;i<2048;++i)assert(!hybris_bc_command_find(handles[i],&owner));
 assert(hybris_bc_command_find(other,&owner));
 hybris_bc_device_remove(d->handle);
 assert(!hybris_bc_command_find(other,&owner) && !owner);
 puts("PASS real BC command lifecycle: allocate, reset success/failure, free, pool destroy, device destroy");
}
