/* SPDX-License-Identifier: Apache-2.0 */
#define VK_NO_PROTOTYPES
#include "polygon_raster.h"
#define PROC(n) PFN_vk##n n=(PFN_vk##n)p->resolver(p->device,"vk" #n)
VkResult hybris_polygon_raster_record(const struct hybris_polygon_pipeline *p,
    const struct hybris_polygon_bindings *bindings,struct hybris_bc_command_state *journal,
    VkCommandBuffer command,VkPolygonMode mode,VkDescriptorSet scratch,
    VkBuffer generated,VkBuffer indirect,const struct hybris_polygon_source *source)
{
    if(!p || !bindings || !journal || !command || !scratch || !generated || !indirect || !source)
        return VK_ERROR_INITIALIZATION_FAILED;
    if(journal->error) return journal->error;
    if(!journal->track_graphics || journal->graphics_pipeline!=p->filled ||
        (mode!=VK_POLYGON_MODE_LINE && mode!=VK_POLYGON_MODE_POINT) ||
        source->first_binding>64 || source->binding_count>64-source->first_binding ||
        (source->binding_count && (!source->vertices || !source->offsets)) ||
        (source->indexed && !source->indices) ||
        (source->indices && source->index_type!=VK_INDEX_TYPE_UINT16 && source->index_type!=VK_INDEX_TYPE_UINT32))
        return VK_ERROR_FEATURE_NOT_PRESENT;
    PROC(CmdBindPipeline);PROC(CmdBindDescriptorSets);PROC(CmdBindIndexBuffer);
    PROC(CmdBindVertexBuffers);PROC(CmdDrawIndexedIndirect);PROC(CmdDrawIndirect);
    if(!CmdBindPipeline || !CmdBindDescriptorSets || !CmdBindIndexBuffer || !CmdBindVertexBuffers ||
        !CmdDrawIndexedIndirect || !CmdDrawIndirect) return VK_ERROR_INITIALIZATION_FAILED;
    VkResult result=hybris_polygon_bindings_bind(bindings,command,VK_PIPELINE_BIND_POINT_GRAPHICS);
    if(result) return result;
    CmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,mode==VK_POLYGON_MODE_LINE ? p->lines : p->points);
    CmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,p->layout,p->reserved_set,1,&scratch,0,NULL);
    CmdBindIndexBuffer(command,generated,0,VK_INDEX_TYPE_UINT32);
    CmdDrawIndexedIndirect(command,indirect,0,1,sizeof(VkDrawIndexedIndirectCommand));
    hybris_bc_restore_graphics(journal,command);
    if(source->binding_count) CmdBindVertexBuffers(command,source->first_binding,source->binding_count,source->vertices,source->offsets);
    if(source->indices)
        CmdBindIndexBuffer(command,source->indices,source->index_offset,source->index_type);
    if(source->indexed) {
        CmdDrawIndexedIndirect(command,indirect,sizeof(VkDrawIndexedIndirectCommand),1,sizeof(VkDrawIndexedIndirectCommand));
    } else CmdDrawIndirect(command,indirect,sizeof(VkDrawIndexedIndirectCommand),1,sizeof(VkDrawIndirectCommand));
    return VK_SUCCESS;
}
