/* SPDX-License-Identifier: Apache-2.0 */
#define VK_NO_PROTOTYPES
#include "rendering_segments.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
static unsigned begins, ends, barriers, failures;
static VkAttachmentLoadOp expected_load;
static void VKAPI_CALL begin(VkCommandBuffer command,const VkRenderingInfo *info)
{
    (void)command;++begins;
    if(info->flags || info->colorAttachmentCount!=12) ++failures;
    for(unsigned i=0;i<info->colorAttachmentCount;++i)
        if(info->pColorAttachments[i].loadOp!=expected_load ||
           info->pColorAttachments[i].storeOp!=VK_ATTACHMENT_STORE_OP_STORE) ++failures;
    if(info->pDepthAttachment->loadOp!=expected_load || info->pStencilAttachment->loadOp!=expected_load ||
       info->pDepthAttachment->storeOp!=VK_ATTACHMENT_STORE_OP_STORE ||
       info->pStencilAttachment->storeOp!=VK_ATTACHMENT_STORE_OP_STORE) ++failures;
}
static void VKAPI_CALL end(VkCommandBuffer command) {(void)command;++ends;}
static void VKAPI_CALL barrier(VkCommandBuffer command,VkPipelineStageFlags source,
    VkPipelineStageFlags destination,VkDependencyFlags flags,uint32_t memory_count,
    const VkMemoryBarrier *memory,uint32_t buffer_count,const VkBufferMemoryBarrier *buffers,
    uint32_t image_count,const VkImageMemoryBarrier *images)
{
    (void)command;(void)flags;(void)buffer_count;(void)buffers;(void)image_count;(void)images;
    ++barriers;
    if(source!=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT || destination!=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT ||
       memory_count!=1 || !(memory->srcAccessMask & VK_ACCESS_MEMORY_WRITE_BIT)) ++failures;
}
static void *VKAPI_CALL fail_alloc(void *user,size_t bytes,size_t alignment,VkSystemAllocationScope scope)
{(void)user;(void)bytes;(void)alignment;(void)scope;return NULL;}
static void VKAPI_CALL free_alloc(void *user,void *memory) {(void)user;(void)memory;++failures;}
#define REQUIRE(condition) do {if(!(condition)) {printf("FAIL line=%d\n",__LINE__);return 1;}} while(0)
int main(void)
{
    struct hybris_rendering_scope scope={0};VkCommandBuffer command=(VkCommandBuffer)(uintptr_t)1;
    VkRenderingAttachmentInfo colors[12],depth={.sType=VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView=(VkImageView)(uintptr_t)2,.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,.storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE};
    VkRenderingAttachmentInfo stencil=depth;
    for(unsigned i=0;i<12;++i) colors[i]=depth;
    VkRenderingInfo info={.sType=VK_STRUCTURE_TYPE_RENDERING_INFO,.colorAttachmentCount=12,
        .pColorAttachments=colors,.pDepthAttachment=&depth,.pStencilAttachment=&stencil};
    VkBaseInStructure unknown={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO};
    colors[4].pNext=&unknown;
    REQUIRE(hybris_rendering_scope_begin(&scope,command,&info,begin,barrier,NULL)==VK_ERROR_FEATURE_NOT_PRESENT);
    REQUIRE(!begins && !barriers && !scope.active);colors[4].pNext=NULL;
    info.flags=VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT;
    REQUIRE(hybris_rendering_scope_begin(&scope,command,&info,begin,barrier,NULL)==VK_ERROR_FEATURE_NOT_PRESENT);
    info.flags=0;
    VkAllocationCallbacks allocator={.pfnAllocation=fail_alloc,.pfnFree=free_alloc};
    REQUIRE(hybris_rendering_scope_begin(&scope,command,&info,begin,barrier,&allocator)==VK_ERROR_OUT_OF_HOST_MEMORY);
    REQUIRE(!begins && !barriers && !scope.active);
    expected_load=VK_ATTACHMENT_LOAD_OP_CLEAR;
    REQUIRE(hybris_rendering_scope_begin(&scope,command,&info,begin,barrier,NULL)==VK_SUCCESS);
    memset(colors,0,sizeof(colors));memset(&depth,0,sizeof(depth));memset(&stencil,0,sizeof(stencil));
    for(unsigned i=0;i<3;++i) {
        REQUIRE(hybris_rendering_scope_pause(&scope,command,end,barrier)==VK_SUCCESS);
        REQUIRE(hybris_rendering_scope_pause(&scope,command,end,barrier)==VK_ERROR_FEATURE_NOT_PRESENT);
        REQUIRE(hybris_rendering_scope_end(&scope,command,end)==VK_ERROR_FEATURE_NOT_PRESENT);
        expected_load=VK_ATTACHMENT_LOAD_OP_LOAD;
        REQUIRE(hybris_rendering_scope_resume(&scope,command,begin,barrier)==VK_SUCCESS);
        REQUIRE(hybris_rendering_scope_resume(&scope,command,begin,barrier)==VK_ERROR_FEATURE_NOT_PRESENT);
    }
    REQUIRE(hybris_rendering_scope_end(&scope,command,end)==VK_SUCCESS);
    REQUIRE(begins==4 && ends==4 && barriers==7 && !failures);
    hybris_rendering_scope_reset(&scope);
    REQUIRE(!scope.colors && !scope.active && !scope.paused);
    puts("RENDERING_SCOPE_PASS color/depth/stencil ownership, repeated pauses, extension rejection, allocation failure");
    return 0;
}
