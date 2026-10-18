#define VK_NO_PROTOTYPES
#include "../../hybris/vulkan/compat/bc_context.c"
#include "../../hybris/vulkan/compat/bc_commands.c"
#include <assert.h>
#include <stdio.h>
static uintptr_t next_handle=1;
static VkResult native_result=VK_SUCCESS;
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
{(void)d;
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
 hybris_bc_layouts_init(&d->layouts,d->handle,resolve);devices=d;
 VkCommandBuffer handles[2048],other;
 VkCommandBufferAllocateInfo info={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
  .commandPool=(VkCommandPool)1,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=2048};
 assert(!allocate_commands(d->handle,&info,handles));
 info.commandPool=(VkCommandPool)2;info.commandBufferCount=1;
 assert(!allocate_commands(d->handle,&info,&other));
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
