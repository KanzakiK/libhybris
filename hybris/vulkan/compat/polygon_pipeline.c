/* SPDX-License-Identifier: Apache-2.0 */
#define VK_NO_PROTOTYPES
#include "polygon_pipeline.h"
#include "vertex_resources.h"
#include <string.h>
#include <pthread.h>
static pthread_mutex_t pipeline_guard=PTHREAD_MUTEX_INITIALIZER;
static struct hybris_polygon_pipeline *pipelines;

void hybris_polygon_pipeline_destroy(struct hybris_polygon_pipeline *p)
{
    if(!p) return;
    const VkAllocationCallbacks *allocator=p->custom_allocator ? &p->allocator : NULL;
    PFN_vkDestroyPipeline destroy=(void*)p->resolver(p->device,"vkDestroyPipeline");
    VkPipeline handles[]={p->producer,p->lines,p->points,p->filled};
    for(unsigned i=0;i<4;++i) if(handles[i]) destroy(p->device,handles[i],allocator);
    if(p->layout) ((PFN_vkDestroyPipelineLayout)p->resolver(p->device,"vkDestroyPipelineLayout"))
        (p->device,p->layout,allocator);
    if(p->sets) {
        PFN_vkDestroyDescriptorSetLayout destroy_set=(void*)p->resolver(p->device,"vkDestroyDescriptorSetLayout");
        for(uint32_t i=0;i<=p->reserved_set;++i) if(p->sets[i]) destroy_set(p->device,p->sets[i],allocator);
    }
    hybris_scaled_free(allocator,p->sets);hybris_scaled_free(allocator,p);
}

static void retire_pipeline(struct hybris_polygon_pipeline *p)
{
    if(p->identity) hybris_bc_layout_release(p->journal_layouts,p->identity);
    if(p->application_layout) hybris_polygon_layout_release(p->application_layout);
    hybris_polygon_pipeline_destroy(p);
}
VkResult hybris_polygon_pipeline_publish(struct hybris_polygon_pipeline *p,
    const struct hybris_polygon_layout *layout,struct hybris_bc_layouts *journal_layouts,
    struct hybris_bc_layout *identity)
{
    if(!p || !p->device || !p->filled || !layout || !journal_layouts || !identity)
        return VK_ERROR_INITIALIZATION_FAILED;
    pthread_mutex_lock(&pipeline_guard);
    for(struct hybris_polygon_pipeline *it=pipelines;it;it=it->next)
        if(it==p || (it->device==p->device && it->filled==p->filled)) {
            pthread_mutex_unlock(&pipeline_guard);return VK_ERROR_INITIALIZATION_FAILED;
        }
    p->application_layout=layout;p->journal_layouts=journal_layouts;p->identity=identity;
    p->references=1;p->next=pipelines;pipelines=p;
    pthread_mutex_unlock(&pipeline_guard);return VK_SUCCESS;
}
struct hybris_polygon_pipeline *hybris_polygon_pipeline_acquire(VkDevice device,VkPipeline handle)
{
    struct hybris_polygon_pipeline *p=NULL;
    pthread_mutex_lock(&pipeline_guard);
    for(struct hybris_polygon_pipeline *it=pipelines;it;it=it->next)
        if(it->device==device && it->filled==handle) {
            if(it->references!=UINT32_MAX) {++it->references;p=it;}
            break;
        }
    pthread_mutex_unlock(&pipeline_guard);return p;
}
void hybris_polygon_pipeline_release(void *object)
{
    struct hybris_polygon_pipeline *p=object;
    if(!p) return;
    pthread_mutex_lock(&pipeline_guard);
    unsigned retire=!--p->references;
    pthread_mutex_unlock(&pipeline_guard);
    if(retire) retire_pipeline(p);
}
int hybris_polygon_pipeline_remove(VkDevice device,VkPipeline handle)
{
    struct hybris_polygon_pipeline *p=NULL;
    pthread_mutex_lock(&pipeline_guard);
    for(struct hybris_polygon_pipeline **link=&pipelines;*link;link=&(*link)->next)
        if((*link)->device==device && (*link)->filled==handle) {
            p=*link;*link=p->next;p->next=NULL;break;
        }
    pthread_mutex_unlock(&pipeline_guard);
    if(p) hybris_polygon_pipeline_release(p);
    return p!=NULL;
}
void hybris_polygon_pipelines_release_device(VkDevice device)
{
    struct hybris_polygon_pipeline *removed=NULL;
    pthread_mutex_lock(&pipeline_guard);
    for(struct hybris_polygon_pipeline **link=&pipelines;*link;) {
        struct hybris_polygon_pipeline *p=*link;
        if(p->device!=device) {link=&p->next;continue;}
        *link=p->next;p->next=removed;removed=p;
    }
    pthread_mutex_unlock(&pipeline_guard);
    while(removed) {
        struct hybris_polygon_pipeline *next=removed->next;removed->next=NULL;
        hybris_polygon_pipeline_release(removed);removed=next;
    }
}

VkResult hybris_polygon_pipeline_create(VkDevice device,PFN_vkGetDeviceProcAddr resolver,
    const VkGraphicsPipelineCreateInfo *graphics,const VkPipelineLayoutCreateInfo *application_layout,
    const VkDescriptorSetLayoutCreateInfo *application_sets,const uint32_t *vertex_code,size_t vertex_size,
    const VkAllocationCallbacks *allocator,struct hybris_polygon_pipeline **out)
{
    *out=NULL;
    if(!graphics || !application_layout || application_layout->pNext || application_layout->flags ||
       (application_layout->setLayoutCount && !application_sets) ||
       (application_layout->pushConstantRangeCount && !application_layout->pPushConstantRanges) || graphics->stageCount!=2 ||
       !graphics->pStages || !graphics->pVertexInputState || !graphics->pInputAssemblyState ||
       !graphics->pRasterizationState || !graphics->pViewportState ||
       graphics->pViewportState->viewportCount!=1 || graphics->pVertexInputState->pNext ||
       graphics->pInputAssemblyState->pNext || graphics->pRasterizationState->pNext ||
       graphics->pInputAssemblyState->topology!=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST ||
       graphics->pInputAssemblyState->primitiveRestartEnable ||
       (graphics->flags & ~VK_PIPELINE_CREATE_DISABLE_OPTIMIZATION_BIT))
        return VK_ERROR_FEATURE_NOT_PRESENT;
    const VkBaseInStructure *chain=graphics->pNext;
    if(chain && (chain->sType!=VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO || chain->pNext ||
        ((const VkPipelineRenderingCreateInfo*)chain)->viewMask)) return VK_ERROR_FEATURE_NOT_PRESENT;
    if(graphics->pDynamicState) {
        if(graphics->pDynamicState->pNext) return VK_ERROR_FEATURE_NOT_PRESENT;
        for(uint32_t i=0;i<graphics->pDynamicState->dynamicStateCount;++i)
            if(graphics->pDynamicState->pDynamicStates[i]>VK_DYNAMIC_STATE_STENCIL_REFERENCE)
                return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    unsigned vertex=2,fragment=2;
    for(unsigned i=0;i<2;++i) {
        if(graphics->pStages[i].flags) return VK_ERROR_FEATURE_NOT_PRESENT;
        if(graphics->pStages[i].stage==VK_SHADER_STAGE_VERTEX_BIT) vertex=i;
        if(graphics->pStages[i].stage==VK_SHADER_STAGE_FRAGMENT_BIT) fragment=i;
    }
    if(vertex==2 || fragment==2) return VK_ERROR_FEATURE_NOT_PRESENT;
    chain=graphics->pStages[vertex].pNext;
    if(chain && (chain->sType!=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO || chain->pNext))
        return VK_ERROR_FEATURE_NOT_PRESENT;
    uint64_t sets_bytes=((uint64_t)application_layout->setLayoutCount+1)*sizeof(VkDescriptorSetLayout);
    if(sets_bytes>SIZE_MAX || application_layout->setLayoutCount==UINT32_MAX)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    struct hybris_polygon_pipeline *p=hybris_scaled_alloc(allocator,sizeof(*p),VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
    if(!p) return VK_ERROR_OUT_OF_HOST_MEMORY;
    memset(p,0,sizeof(*p));p->device=device;p->resolver=resolver;
    p->mode=graphics->pRasterizationState->polygonMode;
    p->cull_mode=graphics->pRasterizationState->cullMode;
    p->front_face=graphics->pRasterizationState->frontFace;
    if(graphics->pViewportState->pViewports) p->viewport=graphics->pViewportState->pViewports[0];
    if(graphics->pDynamicState) for(uint32_t i=0;i<graphics->pDynamicState->dynamicStateCount;++i)
        if(graphics->pDynamicState->pDynamicStates[i]==VK_DYNAMIC_STATE_VIEWPORT) p->dynamic_viewport=VK_TRUE;
    p->custom_allocator=allocator!=NULL;if(allocator) p->allocator=*allocator;
    p->reserved_set=application_layout->setLayoutCount;
    p->sets=hybris_scaled_alloc(allocator,(size_t)sets_bytes,VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
    VkResult result=VK_ERROR_OUT_OF_HOST_MEMORY;
    uint32_t *producer_code=NULL,*replay_code=NULL;size_t producer_size=0,replay_size=0;
    VkShaderModule producer_module=VK_NULL_HANDLE,replay_module=VK_NULL_HANDLE;
    VkPushConstantRange *push=NULL;
    const char *reason=NULL;
    if(!p->sets) goto done;
    memset(p->sets,0,(size_t)sets_bytes);
    result=hybris_spirv_vertex_fetch_compute(vertex_code,vertex_size,p->reserved_set,
        graphics->pVertexInputState,&p->records,allocator,&producer_code,&producer_size,&reason);
    if(result) goto done;
    struct hybris_vertex_compute_layout replay_records;
    result=hybris_spirv_vertex_replay(vertex_code,vertex_size,p->reserved_set,
        &replay_records,allocator,&replay_code,&replay_size,&reason);
    if(result) goto done;
    if(memcmp(&p->records,&replay_records,sizeof(replay_records))) {result=VK_ERROR_FEATURE_NOT_PRESENT;goto done;}
    for(uint32_t i=0;i<p->records.input_count;++i) {
        for(uint32_t j=0;j<graphics->pVertexInputState->vertexAttributeDescriptionCount;++j)
            if(graphics->pVertexInputState->pVertexAttributeDescriptions[j].location==p->records.inputs[i].location)
                p->attributes[i]=graphics->pVertexInputState->pVertexAttributeDescriptions[j];
        for(uint32_t j=0;j<graphics->pVertexInputState->vertexBindingDescriptionCount;++j)
            if(graphics->pVertexInputState->pVertexBindingDescriptions[j].binding==p->attributes[i].binding)
                p->bindings[i]=graphics->pVertexInputState->pVertexBindingDescriptions[j];
    }
    PFN_vkCreateDescriptorSetLayout create_set=(void*)resolver(device,"vkCreateDescriptorSetLayout");
    for(uint32_t i=0;i<p->reserved_set;++i) {
        result=hybris_vertex_resource_layout(device,create_set,&application_sets[i],allocator,&p->sets[i]);
        if(result) goto done;
    }
    VkDescriptorSetLayoutBinding scratch[67];uint32_t scratch_count=p->records.input_count+3;
    for(uint32_t i=0;i<scratch_count;++i) scratch[i]=(VkDescriptorSetLayoutBinding){.binding=i,
        .descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT | (i==1 ? VK_SHADER_STAGE_VERTEX_BIT : 0)};
    VkDescriptorSetLayoutCreateInfo scratch_info={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount=scratch_count,.pBindings=scratch};
    result=create_set(device,&scratch_info,allocator,&p->sets[p->reserved_set]);if(result) goto done;
    uint64_t push_bytes=(uint64_t)application_layout->pushConstantRangeCount*sizeof(*push);
    if(push_bytes>SIZE_MAX) {result=VK_ERROR_OUT_OF_HOST_MEMORY;goto done;}
    if(push_bytes) {
        push=hybris_scaled_alloc(allocator,(size_t)push_bytes,VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
        if(!push) {result=VK_ERROR_OUT_OF_HOST_MEMORY;goto done;}
        memcpy(push,application_layout->pPushConstantRanges,(size_t)push_bytes);
        for(uint32_t i=0;i<application_layout->pushConstantRangeCount;++i)
            if(push[i].stageFlags & VK_SHADER_STAGE_VERTEX_BIT) push[i].stageFlags|=VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkPipelineLayoutCreateInfo layout=*application_layout;
    layout.setLayoutCount=p->reserved_set+1;layout.pSetLayouts=p->sets;layout.pPushConstantRanges=push;
    result=((PFN_vkCreatePipelineLayout)resolver(device,"vkCreatePipelineLayout"))(device,&layout,allocator,&p->layout);
    if(result) goto done;
    PFN_vkCreateShaderModule create_module=(void*)resolver(device,"vkCreateShaderModule");
    VkShaderModuleCreateInfo module={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize=producer_size,.pCode=producer_code};
    result=create_module(device,&module,allocator,&producer_module);if(result) goto done;
    module.codeSize=replay_size;module.pCode=replay_code;
    result=create_module(device,&module,allocator,&replay_module);if(result) goto done;
    VkComputePipelineCreateInfo compute={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .layout=p->layout,.stage=graphics->pStages[vertex]};
    compute.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;compute.stage.module=producer_module;compute.stage.pNext=NULL;
    result=((PFN_vkCreateComputePipelines)resolver(device,"vkCreateComputePipelines"))
        (device,VK_NULL_HANDLE,1,&compute,allocator,&p->producer);if(result) goto done;
    VkPipelineRasterizationStateCreateInfo raster=*graphics->pRasterizationState;
    raster.polygonMode=VK_POLYGON_MODE_FILL;
    VkGraphicsPipelineCreateInfo original=*graphics;original.pRasterizationState=&raster;
    PFN_vkCreateGraphicsPipelines create_graphics=(void*)resolver(device,"vkCreateGraphicsPipelines");
    result=create_graphics(device,VK_NULL_HANDLE,1,&original,allocator,&p->filled);if(result) goto done;
    VkPipelineShaderStageCreateInfo stages[2]={graphics->pStages[0],graphics->pStages[1]};
    stages[vertex].module=replay_module;stages[vertex].pNext=NULL;
    VkPipelineVertexInputStateCreateInfo input={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly=*graphics->pInputAssemblyState;
    raster.cullMode=VK_CULL_MODE_NONE;
    VkGraphicsPipelineCreateInfo replay=*graphics;replay.pStages=stages;replay.layout=p->layout;
    replay.pVertexInputState=&input;replay.pInputAssemblyState=&assembly;replay.pRasterizationState=&raster;
    assembly.topology=VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    result=create_graphics(device,VK_NULL_HANDLE,1,&replay,allocator,&p->lines);if(result) goto done;
    assembly.topology=VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    result=create_graphics(device,VK_NULL_HANDLE,1,&replay,allocator,&p->points);
done:
    if(producer_module) ((PFN_vkDestroyShaderModule)resolver(device,"vkDestroyShaderModule"))(device,producer_module,allocator);
    if(replay_module) ((PFN_vkDestroyShaderModule)resolver(device,"vkDestroyShaderModule"))(device,replay_module,allocator);
    hybris_scaled_free(allocator,push);hybris_scaled_free(allocator,producer_code);hybris_scaled_free(allocator,replay_code);
    if(result) hybris_polygon_pipeline_destroy(p);else *out=p;
    return result;
}
