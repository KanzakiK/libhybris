/* SPDX-License-Identifier: Apache-2.0 */
/* GPU conversion -> indexed-indirect rasterization feasibility probe.
 * A bounded application vertex fixture is lowered natively to compute.
 * General vertex input/interface and draw interception are not implemented.
 * Host readback is an assertion after fence completion, never a draw input.
 * Capacity fallback selects a complete native filled draw, not a prefix. */
#include "probe.h"
#include "polygon-indices.inc"
#include "vertex_compute.h"
#include "rendering_segments.h"
#include "vertex_resources.h"
#include "polygon_pipeline.h"
#include "bc_command_state.h"
#include "polygon_indices.h"
#include "polygon_bindings.h"
#include "polygon_raster.h"
#include "polygon_buffers.h"
#undef CHECK
#define CHECK(expr) do { VkResult result=(expr); if(result!=VK_SUCCESS) { \
  printf("FAIL %s = %d\n",#expr,result); return 2; } } while(0)

static VkResult begin_target(struct hybris_rendering_scope *scope,VkCommandBuffer command,
  VkImage image,VkImageView view,PFN_vkCmdBeginRendering begin,
  PFN_vkCmdPipelineBarrier barrier,PFN_vkCmdClearAttachments clear)
{
  VkImageMemoryBarrier target={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
    .srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,
    .dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,
    .newLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
    .image=image,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
  barrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    0,0,NULL,0,NULL,1,&target);
  VkRenderingAttachmentInfo color={.sType=VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
    .imageView=view,.imageLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    .loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,.storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE};
  VkRenderingInfo info={.sType=VK_STRUCTURE_TYPE_RENDERING_INFO,.renderArea={{0,0},{16,16}},
    .layerCount=1,.colorAttachmentCount=1,.pColorAttachments=&color};
  VkResult result=hybris_rendering_scope_begin(scope,command,&info,begin,barrier,NULL);
  if(result) return result;
  VkClearAttachment marker={.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT,.colorAttachment=0,
    .clearValue.color.float32={1,0,0,1}};
  VkClearRect rect={.rect={{0,0},{1,1}},.baseArrayLayer=0,.layerCount=1};
  clear(command,1,&marker,1,&rect);
  return VK_SUCCESS;
}

int main(int argc, char **argv) {
  int validate=0,instanced=0,dynamic=0,record_overflow=0,own_pipeline=0,restore_state=0,clipped=0,bind_companion=0,gpu_select=0,native_buffers=0,normal_draw=0,normal_overflow=0;uint32_t index_width=4;
  for(int i=1;i<argc;++i) {
    if(!strcmp(argv[i],"--validation")) validate=1;
    else if(!strcmp(argv[i],"--indices16")) index_width=2;
    else if(!strcmp(argv[i],"--direct")) index_width=0;
    else if(!strcmp(argv[i],"--instances")) instanced=1;
    else if(!strcmp(argv[i],"--dynamic")) dynamic=1;
    else if(!strcmp(argv[i],"--record-overflow")) record_overflow=1;
    else if(!strcmp(argv[i],"--pipeline")) own_pipeline=1;
    else if(!strcmp(argv[i],"--restore")) {restore_state=1;own_pipeline=1;}
    else if(!strcmp(argv[i],"--clip")) clipped=1;
    else if(!strcmp(argv[i],"--bindings")) {bind_companion=1;restore_state=1;own_pipeline=1;}
    else if(!strcmp(argv[i],"--gpu-select")) {gpu_select=1;bind_companion=1;restore_state=1;own_pipeline=1;}
    else if(!strcmp(argv[i],"--buffers")) {native_buffers=1;gpu_select=1;bind_companion=1;restore_state=1;own_pipeline=1;}
    else if(!strcmp(argv[i],"--normal-draw")) normal_draw=1;
    else if(!strcmp(argv[i],"--normal-overflow")) {normal_draw=1;normal_overflow=1;instanced=1;}
    else return 2;
  }
  if(normal_draw && (native_buffers || own_pipeline || record_overflow || !dynamic)) return 2;
  if(normal_draw) gpu_select=1;
  void *h=dlopen("libvulkan.so.1", RTLD_NOW|RTLD_LOCAL);
  if (!h) { puts(dlerror()); return 2; }
  PFN_vkGetInstanceProcAddr gip=sym(h,"vkGetInstanceProcAddr");
  VkInstance instance=VK_NULL_HANDLE;
  V(vkCreateInstance);
  VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.pApplicationName="hybris-polygon-compute",.apiVersion=VK_API_VERSION_1_2};
  VkInstanceCreateInfo ic={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};
  const char *layer="VK_LAYER_KHRONOS_validation";
  const char *extensions[]={VK_EXT_DEBUG_UTILS_EXTENSION_NAME,VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME};
  struct validation_state validation={0};
  VkDebugUtilsMessengerCreateInfoEXT debug={.sType=VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
    .messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
    .messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT,
    .pfnUserCallback=validation_message,.pUserData=&validation};
  VkValidationFeatureEnableEXT sync=VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
  VkValidationFeaturesEXT vf={.sType=VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,.pNext=&debug,
    .enabledValidationFeatureCount=1,.pEnabledValidationFeatures=&sync};
  if(validate) {ic.pNext=&vf;ic.enabledLayerCount=1;ic.ppEnabledLayerNames=&layer;
    ic.enabledExtensionCount=2;ic.ppEnabledExtensionNames=extensions;}
  CHECK(p_vkCreateInstance(&ic,NULL,&instance));
  VkDebugUtilsMessengerEXT messenger=VK_NULL_HANDLE;
  PFN_vkDestroyDebugUtilsMessengerEXT destroy_messenger=NULL;
  if(validate) {
    V(vkCreateDebugUtilsMessengerEXT);
    destroy_messenger=(void*)gip(instance,"vkDestroyDebugUtilsMessengerEXT");
    if(!destroy_messenger) return 2;
    CHECK(p_vkCreateDebugUtilsMessengerEXT(instance,&debug,NULL,&messenger));
  }
  V(vkDestroyInstance); V(vkEnumeratePhysicalDevices); V(vkGetPhysicalDeviceQueueFamilyProperties);
  V(vkGetPhysicalDeviceMemoryProperties); V(vkCreateDevice); V(vkDestroyDevice); V(vkGetDeviceQueue);
  V(vkCreateBuffer); V(vkDestroyBuffer); V(vkGetBufferMemoryRequirements); V(vkAllocateMemory);
  V(vkFreeMemory); V(vkBindBufferMemory); V(vkMapMemory); V(vkUnmapMemory);
  V(vkCreateDescriptorSetLayout); V(vkDestroyDescriptorSetLayout); V(vkCreateDescriptorPool);
  V(vkDestroyDescriptorPool); V(vkAllocateDescriptorSets); V(vkUpdateDescriptorSets);
  V(vkCreatePipelineLayout); V(vkDestroyPipelineLayout); V(vkCreateShaderModule); V(vkDestroyShaderModule);
  V(vkCreateComputePipelines); V(vkDestroyPipeline); V(vkCreateCommandPool); V(vkDestroyCommandPool);
  V(vkAllocateCommandBuffers); V(vkBeginCommandBuffer); V(vkEndCommandBuffer); V(vkResetCommandPool);
  V(vkCmdBindPipeline); V(vkCmdBindDescriptorSets); V(vkCmdPushConstants); V(vkCmdDispatch);
  V(vkCmdPipelineBarrier); V(vkCreateFence); V(vkDestroyFence); V(vkResetFences);
  V(vkQueueSubmit); V(vkWaitForFences);
  V(vkCreateImage); V(vkDestroyImage); V(vkGetImageMemoryRequirements); V(vkBindImageMemory);
  V(vkCreateImageView); V(vkDestroyImageView); V(vkCreateRenderPass); V(vkDestroyRenderPass);
  V(vkCreateFramebuffer); V(vkDestroyFramebuffer); V(vkCreateGraphicsPipelines);
  V(vkCmdBeginRenderPass); V(vkCmdEndRenderPass); V(vkCmdBindIndexBuffer);
  V(vkCmdBindVertexBuffers);V(vkCmdDraw);
  V(vkGetDeviceProcAddr); V(vkCmdClearAttachments);
  V(vkCmdDrawIndexedIndirect); V(vkCmdDrawIndexed); V(vkCmdCopyImageToBuffer);
  V(vkCmdDrawIndirect);V(vkCmdCopyBuffer);V(vkGetPhysicalDeviceProperties);
  uint32_t n=1; VkPhysicalDevice pd;
  CHECK(p_vkEnumeratePhysicalDevices(instance,&n,&pd)); if (!n) return 3;
  uint32_t nq=0; p_vkGetPhysicalDeviceQueueFamilyProperties(pd,&nq,NULL);
  VkQueueFamilyProperties *qp=calloc(nq,sizeof(*qp));
  p_vkGetPhysicalDeviceQueueFamilyProperties(pd,&nq,qp);
  uint32_t family=0;
  while(family<nq && (qp[family].queueFlags & (VK_QUEUE_COMPUTE_BIT|VK_QUEUE_GRAPHICS_BIT))!=(VK_QUEUE_COMPUTE_BIT|VK_QUEUE_GRAPHICS_BIT)) ++family;
  free(qp); if(family==nq) return 3;
  float priority=1;
  VkDeviceQueueCreateInfo qc={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
  VkDeviceCreateInfo dc={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qc};
  VkPhysicalDeviceFeatures indirect_features={.drawIndirectFirstInstance=VK_TRUE};
  if(normal_draw) indirect_features.fillModeNonSolid=VK_TRUE;
  if(gpu_select) dc.pEnabledFeatures=&indirect_features;
  VkPhysicalDeviceDynamicRenderingFeatures dynamic_feature={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES,.dynamicRendering=VK_TRUE};
  VkPhysicalDeviceVulkan11Features v11={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,.shaderDrawParameters=VK_TRUE};
  if(own_pipeline || normal_draw) dc.pNext=&v11;
  const char *dynamic_extension=VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME;
  if(dynamic) {dc.pNext=&dynamic_feature;dc.enabledExtensionCount=1;dc.ppEnabledExtensionNames=&dynamic_extension;}
  if(dynamic && (own_pipeline || normal_draw)) dynamic_feature.pNext=&v11;
  VkDevice device; CHECK(p_vkCreateDevice(pd,&dc,NULL,&device));
  PFN_vkCmdBeginRendering begin_render=(void*)p_vkGetDeviceProcAddr(device,"vkCmdBeginRenderingKHR");
  PFN_vkCmdEndRendering end_render=(void*)p_vkGetDeviceProcAddr(device,"vkCmdEndRenderingKHR");
  if(dynamic && (!begin_render || !end_render)) {puts("FAIL missing dynamic rendering commands");return 2;}
  VkQueue queue; p_vkGetDeviceQueue(device,family,0,&queue);
  VkPhysicalDeviceMemoryProperties mp; p_vkGetPhysicalDeviceMemoryProperties(pd,&mp);
  VkPhysicalDeviceProperties properties;p_vkGetPhysicalDeviceProperties(pd,&properties);
  if(normal_overflow && properties.limits.maxComputeWorkGroupCount[0]<32769) {puts("SKIP overflow producer exceeds native dispatch limit");return 2;}
  VkBuffer buffers[10]; VkDeviceMemory memory[10]; void *mapped[10];
  VkDeviceSize sizes[]={288,24,128,40,2048,48,192,24,32,512};
  for(unsigned i=0;i<10;++i) {
    VkBufferCreateInfo bc={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=sizes[i],
      .usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | (i==1 || i==2 || i==5 || i==8 ? VK_BUFFER_USAGE_INDEX_BUFFER_BIT : 0) |
        (i==6 ? VK_BUFFER_USAGE_VERTEX_BUFFER_BIT : 0) |
        (i==3 ? VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT : 0) | (i==0 || i==2 || i==3 || i==4 ? VK_BUFFER_USAGE_TRANSFER_DST_BIT : 0) |
        (i==9 ? VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT : 0)};
    CHECK(p_vkCreateBuffer(device,&bc,NULL,&buffers[i]));
    VkMemoryRequirements mr; p_vkGetBufferMemoryRequirements(device,buffers[i],&mr);
    int mi=find_mem(&mp,mr.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if(mi<0) return 3;
    VkMemoryAllocateInfo ma={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=mr.size,.memoryTypeIndex=(uint32_t)mi};
    CHECK(p_vkAllocateMemory(device,&ma,NULL,&memory[i])); CHECK(p_vkBindBufferMemory(device,buffers[i],memory[i],0));
    CHECK(p_vkMapMemory(device,memory[i],0,sizes[i],0,&mapped[i]));
  }
  const float positions[6][4]={{-.5f,-.5f,0,1},{.5f,-.5f,0,1},{-.5f,.5f,0,1},
    {-.5f,-.5f,0,1},{-.5f,.5f,0,1},{.5f,-.5f,0,1}};
  const uint32_t input[]={0,1,2,3,4,5};
  memset(mapped[6],0xa5,sizes[6]);
  for(unsigned v=0;v<6;++v) memcpy((unsigned char*)mapped[6]+v*32+8,positions[v],16);
  memcpy(mapped[1],input,sizeof(input));
  const uint32_t draw_parameters[6]={0,1,UINT32_MAX,instanced ? 5u : 0u,instanced ? 3u : 6u,index_width};
  const uint32_t fetch_indices[7]={0,2,3,1,5,6,4};
  memcpy(mapped[7],draw_parameters,sizeof(draw_parameters));memcpy(mapped[8],fetch_indices,sizeof(fetch_indices));
  if(index_width==2) for(unsigned i=0;i<7;++i) ((uint16_t*)mapped[8])[i]=(uint16_t)fetch_indices[i];
  const float resource_bias[]={.05f,.02f,0,0};
  memset(mapped[9],0,sizes[9]);memcpy((unsigned char*)mapped[9]+256,resource_bias,sizeof(resource_bias));
  VkDescriptorSetLayoutBinding bindings[4];
  for(unsigned i=0;i<4;++i) bindings[i]=(VkDescriptorSetLayoutBinding){.binding=i,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=1,
    .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT|(i==0 ? VK_SHADER_STAGE_VERTEX_BIT : 0)};
  VkDescriptorSetLayoutCreateInfo slc={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=4,.pBindings=bindings};
  VkDescriptorSetLayout sl; CHECK(p_vkCreateDescriptorSetLayout(device,&slc,NULL,&sl));
  VkDescriptorPoolSize ps={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,4};
  VkDescriptorPoolCreateInfo dpc={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=1,.pPoolSizes=&ps};
  VkDescriptorPool pool; CHECK(p_vkCreateDescriptorPool(device,&dpc,NULL,&pool));
  VkDescriptorSetAllocateInfo sa={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=pool,.descriptorSetCount=1,.pSetLayouts=&sl};
  VkDescriptorSet set; CHECK(p_vkAllocateDescriptorSets(device,&sa,&set));
  VkDescriptorBufferInfo bi[4]; VkWriteDescriptorSet writes[4];
  for(unsigned i=0;i<4;++i) {
    bi[i]=(VkDescriptorBufferInfo){buffers[i],0,sizes[i]};
    writes[i]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=i,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&bi[i]};
  }
  p_vkUpdateDescriptorSets(device,4,writes,0,NULL);
  VkPushConstantRange push={VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(struct hybris_polygon_push)};
  VkPipelineLayoutCreateInfo plc={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&sl,.pushConstantRangeCount=1,.pPushConstantRanges=&push};
  struct hybris_polygon_indices converter;
  CHECK(hybris_polygon_indices_create(device,p_vkGetDeviceProcAddr,NULL,&converter));
  VkPipelineLayout layout=converter.layout;plc.pSetLayouts=&converter.descriptors;
  VkDescriptorPool converter_pool;CHECK(p_vkCreateDescriptorPool(device,&dpc,NULL,&converter_pool));
  VkDescriptorSetAllocateInfo converter_alloc=sa;converter_alloc.descriptorPool=converter_pool;
  converter_alloc.pSetLayouts=&converter.descriptors;
  VkDescriptorSet converter_set;CHECK(p_vkAllocateDescriptorSets(device,&converter_alloc,&converter_set));
  for(unsigned i=0;i<4;++i) writes[i].dstSet=converter_set;
  p_vkUpdateDescriptorSets(device,4,writes,0,NULL);
  VkComputePipelineCreateInfo pc={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.layout=layout,
    .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.pName="main"}};
  VkPipeline pipeline=converter.pipeline;
  VkDescriptorSetLayoutBinding producer_bindings[4]={bindings[0],bindings[1],bindings[2],bindings[3]};
  producer_bindings[1].stageFlags|=VK_SHADER_STAGE_VERTEX_BIT;
  VkDescriptorSetLayoutCreateInfo producer_slc=slc; producer_slc.bindingCount=4;producer_slc.pBindings=producer_bindings;
  VkDescriptorSetLayout producer_sl;
  CHECK(p_vkCreateDescriptorSetLayout(device,&producer_slc,NULL,&producer_sl));
  VkDescriptorPoolSize producer_ps={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,4};
  VkDescriptorPoolCreateInfo producer_dpc=dpc; producer_dpc.pPoolSizes=&producer_ps;
  VkDescriptorPool producer_pool;CHECK(p_vkCreateDescriptorPool(device,&producer_dpc,NULL,&producer_pool));
  VkDescriptorSetAllocateInfo producer_sa=sa;producer_sa.descriptorPool=producer_pool;producer_sa.pSetLayouts=&producer_sl;
  VkDescriptorSet producer_set;CHECK(p_vkAllocateDescriptorSets(device,&producer_sa,&producer_set));
  VkDescriptorBufferInfo producer_bi[4]={{buffers[7],0,sizes[7]},{buffers[0],0,sizes[0]},{buffers[6],0,sizes[6]},{buffers[8],0,sizes[8]}};
  VkWriteDescriptorSet producer_writes[4];
  for(unsigned j=0;j<4;++j) producer_writes[j]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
    .dstSet=producer_set,.dstBinding=j,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&producer_bi[j]};
  p_vkUpdateDescriptorSets(device,4,producer_writes,0,NULL);
  VkDescriptorSetLayoutBinding resource_binding={.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
    .descriptorCount=1,.stageFlags=VK_SHADER_STAGE_VERTEX_BIT};
  VkDescriptorSetLayoutCreateInfo resource_slc={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    .bindingCount=1,.pBindings=&resource_binding};
  VkDescriptorSetLayout original_resource_layout,compute_resource_layout;
  CHECK(p_vkCreateDescriptorSetLayout(device,&resource_slc,NULL,&original_resource_layout));
  CHECK(hybris_vertex_resource_layout(device,p_vkCreateDescriptorSetLayout,&resource_slc,NULL,&compute_resource_layout));
  VkDescriptorPoolSize resource_pool_size={VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,2};
  VkDescriptorPoolCreateInfo resource_pool_info={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
    .maxSets=2,.poolSizeCount=1,.pPoolSizes=&resource_pool_size};
  VkDescriptorPool resource_pool;CHECK(p_vkCreateDescriptorPool(device,&resource_pool_info,NULL,&resource_pool));
  VkDescriptorSetLayout resource_layouts[]={original_resource_layout,compute_resource_layout};
  VkDescriptorSetAllocateInfo resource_alloc={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
    .descriptorPool=resource_pool,.descriptorSetCount=2,.pSetLayouts=resource_layouts};
  VkDescriptorSet resource_sets[2];CHECK(p_vkAllocateDescriptorSets(device,&resource_alloc,resource_sets));
  VkDescriptorBufferInfo resource_buffer={buffers[9],0,sizeof(resource_bias)};
  VkWriteDescriptorSet resource_write={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
    .dstSet=resource_sets[0],.dstBinding=0,.descriptorCount=1,
    .descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,.pBufferInfo=&resource_buffer};
  p_vkUpdateDescriptorSets(device,1,&resource_write,0,NULL);
  VkDescriptorSetLayout producer_layouts[]={producer_sl,compute_resource_layout};
  VkPipelineLayoutCreateInfo producer_plc=plc;producer_plc.setLayoutCount=2;producer_plc.pSetLayouts=producer_layouts;
  VkPipelineLayout producer_layout;CHECK(p_vkCreatePipelineLayout(device,&producer_plc,NULL,&producer_layout));
  uint32_t *converted=NULL;size_t converted_size=0;const char *reason=NULL;
  struct hybris_vertex_compute_layout producer_plan;
  VkVertexInputBindingDescription fetch_binding={.binding=7,.stride=32,.inputRate=VK_VERTEX_INPUT_RATE_VERTEX};
  VkVertexInputAttributeDescription fetch_attribute={.location=0,.binding=7,.format=VK_FORMAT_R32G32B32A32_SFLOAT,.offset=8};
  VkPipelineVertexInputStateCreateInfo fetch_info={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    .vertexBindingDescriptionCount=1,.pVertexBindingDescriptions=&fetch_binding,
    .vertexAttributeDescriptionCount=1,.pVertexAttributeDescriptions=&fetch_attribute};
  const uint32_t *source_code=instanced ? vertex_instance_source_spv : vertex_compute_source_spv;
  size_t source_size=instanced ? sizeof(vertex_instance_source_spv) : sizeof(vertex_compute_source_spv);
  VkResult lower=hybris_spirv_vertex_fetch_compute(source_code,source_size,0,&fetch_info,&producer_plan,NULL,&converted,&converted_size,&reason);
  if(lower) {printf("FAIL vertex lowering=%d reason=%s\n",lower,reason);return 1;}
  if(producer_plan.input_stride!=16 || producer_plan.output_stride!=48) {puts("FAIL producer fixture layout");return 1;}
  VkShaderModuleCreateInfo producer_smc={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=converted_size,.pCode=converted};
  VkShaderModule producer_shader;CHECK(p_vkCreateShaderModule(device,&producer_smc,NULL,&producer_shader));free(converted);
  VkComputePipelineCreateInfo producer_pc=pc;producer_pc.layout=producer_layout;producer_pc.stage.module=producer_shader;
  VkPipeline producer;CHECK(p_vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&producer_pc,NULL,&producer));
  struct hybris_vertex_compute_layout replay_plan;
  CHECK(hybris_spirv_vertex_replay(source_code,source_size,0,&replay_plan,NULL,&converted,&converted_size,&reason));
  if(memcmp(&replay_plan,&producer_plan,sizeof(replay_plan))) {puts("FAIL producer/replay layout mismatch");return 1;}
  VkShaderModuleCreateInfo vsc={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=converted_size,.pCode=converted};
  VkShaderModuleCreateInfo fsc={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=sizeof(polygon_replay_frag_spv),.pCode=polygon_replay_frag_spv};
  VkShaderModule vs,fs; CHECK(p_vkCreateShaderModule(device,&vsc,NULL,&vs));free(converted); CHECK(p_vkCreateShaderModule(device,&fsc,NULL,&fs));
  VkImageCreateInfo imgc={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,.format=VK_FORMAT_R8G8B8A8_UNORM,
    .extent={16,16,1},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,
    .usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT};
  VkImage image; CHECK(p_vkCreateImage(device,&imgc,NULL,&image));
  VkMemoryRequirements imr; p_vkGetImageMemoryRequirements(device,image,&imr);
  int imi=find_mem(&mp,imr.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if(imi<0) imi=find_mem(&mp,imr.memoryTypeBits,0);
  if(imi<0) return 3;
  VkMemoryAllocateInfo ima={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=imr.size,.memoryTypeIndex=(uint32_t)imi};
  VkDeviceMemory imem; CHECK(p_vkAllocateMemory(device,&ima,NULL,&imem)); CHECK(p_vkBindImageMemory(device,image,imem,0));
  VkImageViewCreateInfo ivc={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=image,.viewType=VK_IMAGE_VIEW_TYPE_2D,
    .format=VK_FORMAT_R8G8B8A8_UNORM,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
  VkImageView view; CHECK(p_vkCreateImageView(device,&ivc,NULL,&view));
  VkAttachmentDescription att={.format=VK_FORMAT_R8G8B8A8_UNORM,.samples=VK_SAMPLE_COUNT_1_BIT,
    .loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,
    .stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,
    .initialLayout=VK_IMAGE_LAYOUT_UNDEFINED,.finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
  VkAttachmentReference color={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription sub={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,.colorAttachmentCount=1,.pColorAttachments=&color};
  VkSubpassDependency deps[2]={
    {.srcSubpass=VK_SUBPASS_EXTERNAL,.dstSubpass=0,.srcStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT,
     .dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT,.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT},
    {.srcSubpass=0,.dstSubpass=VK_SUBPASS_EXTERNAL,.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
     .dstStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT,.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT}};
  VkRenderPassCreateInfo rpc={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.attachmentCount=1,.pAttachments=&att,
    .subpassCount=1,.pSubpasses=&sub,.dependencyCount=2,.pDependencies=deps};
  VkRenderPass rp; CHECK(p_vkCreateRenderPass(device,&rpc,NULL,&rp));
  VkFramebufferCreateInfo fbc={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=rp,.attachmentCount=1,.pAttachments=&view,.width=16,.height=16,.layers=1};
  VkFramebuffer fb; CHECK(p_vkCreateFramebuffer(device,&fbc,NULL,&fb));
  VkPipelineShaderStageCreateInfo stages[2]={
    {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=vs,.pName="main"},
    {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=fs,.pName="main"}};
  VkPipelineVertexInputStateCreateInfo vi={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  VkPipelineInputAssemblyStateCreateInfo ia={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  VkViewport viewport={0,0,16,16,0,1}; VkRect2D scissor={{0,0},{16,16}};
  VkPipelineViewportStateCreateInfo vps={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,.viewportCount=1,.pViewports=&viewport,.scissorCount=1,.pScissors=&scissor};
  VkPipelineRasterizationStateCreateInfo raster={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,.polygonMode=VK_POLYGON_MODE_FILL,.lineWidth=1};
  VkPipelineMultisampleStateCreateInfo ms={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};
  VkPipelineColorBlendAttachmentState blend={.colorWriteMask=15};
  VkPipelineColorBlendStateCreateInfo bs={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,.attachmentCount=1,.pAttachments=&blend};
  VkGraphicsPipelineCreateInfo gp={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,.stageCount=2,.pStages=stages,
    .pVertexInputState=&vi,.pInputAssemblyState=&ia,.pViewportState=&vps,.pRasterizationState=&raster,
    .pMultisampleState=&ms,.pColorBlendState=&bs,.layout=producer_layout,.renderPass=rp};
  VkFormat color_format=VK_FORMAT_R8G8B8A8_UNORM;
  VkPipelineRenderingCreateInfo pipeline_rendering={.sType=VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
    .colorAttachmentCount=1,.pColorAttachmentFormats=&color_format};
  if(dynamic) {gp.renderPass=VK_NULL_HANDLE;gp.pNext=&pipeline_rendering;}
  VkDescriptorSetLayout application_layouts[]={producer_sl,original_resource_layout};
  VkPushConstantRange application_push={VK_SHADER_STAGE_VERTEX_BIT,0,32};
  VkPipelineLayoutCreateInfo application_layout_info={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .setLayoutCount=2,.pSetLayouts=application_layouts,.pushConstantRangeCount=1,.pPushConstantRanges=&application_push};
  VkPipelineLayout application_layout=VK_NULL_HANDLE;VkShaderModule original_vertex=VK_NULL_HANDLE;
  VkDescriptorSetLayoutCreateInfo application_set_info[]={producer_slc,resource_slc};
  if(own_pipeline || normal_draw) {
    CHECK(p_vkCreatePipelineLayout(device,&application_layout_info,NULL,&application_layout));
    VkShaderModuleCreateInfo original_info={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=source_size,.pCode=source_code};
    CHECK(p_vkCreateShaderModule(device,&original_info,NULL,&original_vertex));
  }
  VkCommandPoolCreateInfo cpc={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=family};
  VkCommandPool cp; CHECK(p_vkCreateCommandPool(device,&cpc,NULL,&cp));
  VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=cp,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
  VkCommandBuffer cmd; CHECK(p_vkAllocateCommandBuffers(device,&ca,&cmd));
  VkFenceCreateInfo fc={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; VkFence fence;
  CHECK(p_vkCreateFence(device,&fc,NULL,&fence));
  unsigned cases=0, filled_cases=0;
  struct hybris_bc_layouts journal_layouts;
  struct hybris_bc_command_state journal;
  struct hybris_bc_layout *application_identity=NULL;
  if(restore_state) {
    hybris_bc_layouts_init(&journal_layouts,device,p_vkGetDeviceProcAddr);
    CHECK(hybris_bc_layout_add(&journal_layouts,layout,&plc));
    CHECK(hybris_bc_layout_add(&journal_layouts,application_layout,&application_layout_info));
    CHECK(hybris_bc_layout_acquire(&journal_layouts,application_layout,&application_identity));
    hybris_bc_command_state_init(&journal,&journal_layouts);
    hybris_bc_command_state_track_graphics(&journal);
  }
  const uint32_t capacities[]={0,2,3,6,12};
  for(unsigned mode=1;mode<=(normal_overflow ? 1u : 2u);++mode) for(unsigned cull=0;cull<4;++cull)
  for(unsigned ccw=0;ccw<2;++ccw) for(unsigned flip=0;flip<2;++flip)
  for(unsigned cap=normal_draw ? 4 : 0;cap<5;++cap) for(unsigned records=native_buffers || normal_draw ? 6 : 3;records<=6;records+=3) {
    CHECK(p_vkResetCommandPool(device,cp,0)); CHECK(p_vkResetFences(device,1,&fence));
    uint32_t resource_count=1;
    CHECK(hybris_vertex_resource_copy(device,p_vkUpdateDescriptorSets,resource_sets[0],resource_sets[1],
      1,&resource_binding,&resource_count,NULL));
    memset(mapped[2],0xcd,sizes[2]); memset(mapped[3],0xcd,sizes[3]);
    memset(mapped[0],0xcd,sizes[0]);
    const int fallback=normal_overflow || clipped || records<6 || record_overflow || 2*(mode==1 ? 6u : 3u)>capacities[cap];
    uint32_t expected[12], count=0;
    if(!fallback) for(unsigned t=0;t<2;++t) {
      if(3*t+2>=records) continue;
      int triangle=instanced ? 0 : (int)t;
      int front=(ccw ? triangle==1 : triangle==0) ^ flip;
      if(cull & (front ? 1 : 2)) continue;
      unsigned base=3*t;
      const unsigned edges[]={0,1,1,2,2,0};
      for(unsigned j=0;j<(mode==1 ? 6u : 3u);++j) expected[count++]=base+(mode==1 ? edges[j] : j);
    }
    memcpy(mapped[5],expected,count*4);
    viewport.y=flip ? 16 : 0; viewport.height=flip ? -16 : 16;
    ia.topology=mode==1 ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST : VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    raster.cullMode=VK_CULL_MODE_NONE;
    VkPipeline lines,filled;
    CHECK(p_vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&gp,NULL,&lines));
    ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    raster.cullMode=cull; raster.frontFace=ccw ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
    CHECK(p_vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&gp,NULL,&filled));
    struct hybris_polygon_pipeline *owned=NULL;
    VkPipeline normal_pipeline=VK_NULL_HANDLE;
    VkPipeline normal_reference=VK_NULL_HANDLE;
    VkDescriptorPool owned_pool=VK_NULL_HANDLE;VkDescriptorSet owned_sets[2];
    if(own_pipeline) {
      VkPipelineShaderStageCreateInfo original_stages[2]={stages[0],stages[1]};original_stages[0].module=original_vertex;
      VkGraphicsPipelineCreateInfo original_graphics=gp;original_graphics.pStages=original_stages;
      original_graphics.layout=application_layout;original_graphics.pVertexInputState=&fetch_info;
      CHECK(hybris_polygon_pipeline_create(device,p_vkGetDeviceProcAddr,&original_graphics,&application_layout_info,
        application_set_info,source_code,source_size,NULL,&owned));
      if(owned->reserved_set!=2 || memcmp(&owned->records,&producer_plan,sizeof(producer_plan))) {puts("FAIL owned pipeline ABI");return 1;}
      VkDescriptorPoolSize owned_sizes[]={{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,1},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,4}};
      VkDescriptorPoolCreateInfo owned_info={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=2,
        .poolSizeCount=2,.pPoolSizes=owned_sizes};
      CHECK(p_vkCreateDescriptorPool(device,&owned_info,NULL,&owned_pool));
      VkDescriptorSetLayout owned_layouts[]={owned->sets[1],owned->sets[2]};
      VkDescriptorSetAllocateInfo owned_alloc={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool=owned_pool,.descriptorSetCount=2,.pSetLayouts=owned_layouts};
      CHECK(p_vkAllocateDescriptorSets(device,&owned_alloc,owned_sets));
      CHECK(hybris_vertex_resource_copy(device,p_vkUpdateDescriptorSets,resource_sets[0],owned_sets[0],1,
        &resource_binding,&resource_count,NULL));
      VkWriteDescriptorSet owned_writes[4];memcpy(owned_writes,producer_writes,sizeof(owned_writes));
      for(unsigned j=0;j<4;++j) owned_writes[j].dstSet=owned_sets[1];
      p_vkUpdateDescriptorSets(device,4,owned_writes,0,NULL);
    }
    if(normal_draw) {
      VkPipelineShaderStageCreateInfo original_stages[2]={stages[0],stages[1]};original_stages[0].module=original_vertex;
      VkGraphicsPipelineCreateInfo original_graphics=gp;original_graphics.pStages=original_stages;
      VkPipelineRasterizationStateCreateInfo requested_raster=raster;requested_raster.polygonMode=(VkPolygonMode)mode;
      original_graphics.pRasterizationState=&requested_raster;
      original_graphics.layout=application_layout;original_graphics.pVertexInputState=&fetch_info;
      /* The target must only call standard Vulkan APIs. The layer owns all
       * lowering, scratch buffers, scope interruption and state restoration. */
      CHECK(p_vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&original_graphics,NULL,&normal_pipeline));
      if(normal_overflow) {
        requested_raster.polygonMode=VK_POLYGON_MODE_FILL;
        CHECK(p_vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&original_graphics,NULL,&normal_reference));
      }
    }
    uint32_t params[]={2,mode,cull,ccw,flip,records,capacities[cap],
      record_overflow ? 100u : producer_plan.output_stride/16,
      gpu_select ? (instanced ? 3u : 6u) : 6u,
      gpu_select && instanced ? 2u : 1u,
      gpu_select && index_width ? 1u : 0u,
      gpu_select ? (index_width ? UINT32_MAX : (instanced ? 5u : 0u)) : 0u,
      gpu_select && index_width && instanced ? 5u : 0u};
    VkCommandBufferBeginInfo cb={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    CHECK(p_vkBeginCommandBuffer(cmd,&cb));
    struct hybris_rendering_scope scope={0};
    if(dynamic) {
      CHECK(begin_target(&scope,cmd,image,view,begin_render,p_vkCmdPipelineBarrier,p_vkCmdClearAttachments));
      CHECK(hybris_rendering_scope_pause(&scope,cmd,end_render,p_vkCmdPipelineBarrier));
    }
    const float transform[]={clipped ? 1.8f : .8f,.75f,1,1,.1f,-.05f,0,0};
    uint32_t resource_offset=256;
    struct hybris_polygon_bindings *companions=NULL;
    struct hybris_polygon_buffers *scratch=NULL;
    if(restore_state) {
      p_vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
      p_vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&converter_set,0,NULL);
      p_vkCmdPushConstants(cmd,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(params),params);
      hybris_bc_save_pipeline(&journal,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
      hybris_bc_save_descriptors(&journal,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&converter_set,0,NULL);
      hybris_bc_save_push(&journal,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(params),params);
      p_vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,owned->filled);
      p_vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,application_layout,1,1,&resource_sets[0],1,&resource_offset);
      p_vkCmdPushConstants(cmd,application_layout,VK_SHADER_STAGE_VERTEX_BIT,0,32,transform);
      hybris_bc_save_pipeline(&journal,VK_PIPELINE_BIND_POINT_GRAPHICS,owned->filled);
      if(bind_companion) {
        uint32_t previous_offset=0;
        hybris_bc_save_descriptors(&journal,VK_PIPELINE_BIND_POINT_GRAPHICS,application_layout,0,1,&producer_set,0,NULL);
        hybris_bc_save_descriptors(&journal,VK_PIPELINE_BIND_POINT_GRAPHICS,application_layout,1,1,&resource_sets[0],1,&previous_offset);
      }
      hybris_bc_save_descriptors(&journal,VK_PIPELINE_BIND_POINT_GRAPHICS,application_layout,1,1,&resource_sets[0],1,&resource_offset);
      hybris_bc_save_push(&journal,application_layout,VK_SHADER_STAGE_VERTEX_BIT,0,32,transform);
      CHECK(journal.error);
      if(bind_companion) CHECK(hybris_polygon_bindings_prepare(owned,application_set_info,
          application_identity,&journal,NULL,&companions));
    }
    if(native_buffers) {
      if(record_overflow) {puts("FAIL --buffers cannot synthesize malformed record ABI");return 2;}
      struct hybris_polygon_draw draw={.indexed=index_width!=0,.count=instanced ? 3 : 6,
        .instances=instanced ? 2 : 1,.first=index_width ? 1 : 0,.first_instance=instanced ? 5 : 0,
        .vertex_offset=-1,.index_type=index_width==2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32,
        .mode=(VkPolygonMode)mode,.cull_mode=cull,.front_face=ccw ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE,
        .inverted_viewport=flip,.index_capacity=capacities[cap]};
      VkDescriptorBufferInfo attribute={buffers[6],0,sizes[6]},indices={buffers[8],0,sizes[8]};
      CHECK(hybris_polygon_buffers_create(owned,&converter,&mp,&properties.limits,&draw,&attribute,&indices,NULL,&scratch));
      CHECK(hybris_polygon_buffers_compute(scratch,owned,&converter,companions,cmd));
      /* Test-only GPU copies after compute. Production buffers are never CPU
       * mapped and the render branch below is still selected by GPU counts. */
      VkMemoryBarrier readable={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT};
      p_vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,1,&readable,0,NULL,0,NULL);
      const unsigned source_slots[]={1,3,4},destination_slots[]={0,2,3};
      for(unsigned j=0;j<3;++j) {
        unsigned from=source_slots[j],to=destination_slots[j];
        VkBufferCopy copy={.size=scratch->sizes[from]<sizes[to] ? scratch->sizes[from] : sizes[to]};
        p_vkCmdCopyBuffer(cmd,scratch->buffers[from],buffers[to],1,&copy);
      }
      hybris_bc_restore_compute(&journal,cmd);
    } else {
    VkPipelineLayout compute_layout=owned ? owned->layout : producer_layout;
    p_vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,owned ? owned->producer : producer);
    p_vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,compute_layout,owned ? owned->reserved_set : 0,
      1,owned ? &owned_sets[1] : &producer_set,0,NULL);
    if(companions) CHECK(hybris_polygon_bindings_bind(companions,cmd,VK_PIPELINE_BIND_POINT_COMPUTE));
    else {
      p_vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,compute_layout,1,1,
        owned ? &owned_sets[0] : &resource_sets[1],1,&resource_offset);
      p_vkCmdPushConstants(cmd,compute_layout,VK_SHADER_STAGE_COMPUTE_BIT | (owned ? VK_SHADER_STAGE_VERTEX_BIT : 0),0,32,transform);
    }
    p_vkCmdDispatch(cmd,6,1,1);
    VkMemoryBarrier records_ready={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,
      .dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_HOST_READ_BIT};
    p_vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_VERTEX_SHADER_BIT|VK_PIPELINE_STAGE_HOST_BIT,0,1,&records_ready,0,NULL,0,NULL);
    if(restore_state) hybris_bc_restore_compute(&journal,cmd);
    else {
      struct hybris_polygon_push conversion;memcpy(&conversion,params,sizeof(conversion));
      CHECK(hybris_polygon_indices_record(&converter,cmd,converter_set,&conversion));
    }
    if(restore_state) p_vkCmdDispatch(cmd,1,1,1);
    }
    VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,
      .dstAccessMask=VK_ACCESS_HOST_READ_BIT|VK_ACCESS_INDEX_READ_BIT|VK_ACCESS_INDIRECT_COMMAND_READ_BIT};
    p_vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_VERTEX_INPUT_BIT|VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,0,1,&barrier,0,NULL,0,NULL);
    VkClearValue clear={.color={{0,0,0,0}}};
    VkRenderPassBeginInfo rb={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=rp,.framebuffer=fb,
      .renderArea={{0,0},{16,16}},.clearValueCount=1,.pClearValues=&clear};
    for(unsigned reference=0;reference<2;++reference) {
      if(dynamic) {
        if(!reference) CHECK(hybris_rendering_scope_resume(&scope,cmd,begin_render,p_vkCmdPipelineBarrier));
        else CHECK(begin_target(&scope,cmd,image,view,begin_render,p_vkCmdPipelineBarrier,p_vkCmdClearAttachments));
      } else p_vkCmdBeginRenderPass(cmd,&rb,VK_SUBPASS_CONTENTS_INLINE);
      if(!normal_draw || reference) p_vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,producer_layout,0,1,&producer_set,0,NULL);
      if(!reference) {
        if(normal_draw) {
          VkDescriptorSet original_sets[]={producer_set,resource_sets[0]};
          p_vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,normal_pipeline);
          p_vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,application_layout,0,2,original_sets,1,&resource_offset);
          p_vkCmdPushConstants(cmd,application_layout,VK_SHADER_STAGE_VERTEX_BIT,0,32,transform);
          VkDeviceSize vertex_offset=0;p_vkCmdBindVertexBuffers(cmd,7,1,&buffers[6],&vertex_offset);
          if(index_width) {
            p_vkCmdBindIndexBuffer(cmd,buffers[8],0,index_width==2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
            p_vkCmdDrawIndexed(cmd,instanced ? 3 : 6,normal_overflow ? 10923 : instanced ? 2 : 1,1,-1,instanced ? 5 : 0);
          } else p_vkCmdDraw(cmd,instanced ? 3 : 6,normal_overflow ? 10923 : instanced ? 2 : 1,0,instanced ? 5 : 0);
        } else if(gpu_select) {
          VkDeviceSize vertex_offset=0;
          struct hybris_polygon_source original={.first_binding=7,.binding_count=1,.vertices=&buffers[6],
            .offsets=&vertex_offset,.indexed=index_width!=0,.indices=buffers[8],.index_offset=0,
            .index_type=index_width==2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32};
          CHECK(hybris_polygon_raster_record(owned,companions,&journal,cmd,
            mode==1 ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_POINT,
            scratch ? scratch->producer_set : owned_sets[1],
            scratch ? scratch->buffers[3] : buffers[2],scratch ? scratch->buffers[4] : buffers[3],&original));
        } else if(owned && fallback) {
          if(restore_state) hybris_bc_restore_graphics(&journal,cmd);
          else {
          p_vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,owned->filled);
          p_vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,application_layout,1,1,&resource_sets[0],1,&resource_offset);
          p_vkCmdPushConstants(cmd,application_layout,VK_SHADER_STAGE_VERTEX_BIT,0,32,transform);
          }
          VkDeviceSize vertex_offset=0;p_vkCmdBindVertexBuffers(cmd,7,1,&buffers[6],&vertex_offset);
          if(index_width) {
            p_vkCmdBindIndexBuffer(cmd,buffers[8],0,index_width==2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
            p_vkCmdDrawIndexed(cmd,instanced ? 3 : 6,instanced ? 2 : 1,1,-1,instanced ? 5 : 0);
          } else p_vkCmdDraw(cmd,instanced ? 3 : 6,instanced ? 2 : 1,0,instanced ? 5 : 0);
        } else {
          p_vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,owned ? (mode==1 ? owned->lines : owned->points) : lines);
          if(companions) CHECK(hybris_polygon_bindings_bind(companions,cmd,VK_PIPELINE_BIND_POINT_GRAPHICS));
          if(owned) p_vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,owned->layout,owned->reserved_set,1,&owned_sets[1],0,NULL);
          p_vkCmdBindIndexBuffer(cmd,buffers[2],0,VK_INDEX_TYPE_UINT32);
          p_vkCmdDrawIndexedIndirect(cmd,buffers[3],0,1,20);
          if(!owned) {
            p_vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,filled);
            p_vkCmdBindIndexBuffer(cmd,buffers[1],0,VK_INDEX_TYPE_UINT32);
            p_vkCmdDrawIndexedIndirect(cmd,buffers[3],20,1,20);
          }
        }
      } else {
        if(normal_overflow) {
          /* 32769 post-VS vertices need 65538 line indices, exceeding the
           * internal 65536 cap. Compare with the complete original filled
           * draw, including all 10923 instances and nonzero firstInstance. */
          VkDescriptorSet original_sets[]={producer_set,resource_sets[0]};
          p_vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,normal_reference);
          p_vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,application_layout,0,2,original_sets,1,&resource_offset);
          p_vkCmdPushConstants(cmd,application_layout,VK_SHADER_STAGE_VERTEX_BIT,0,32,transform);
          VkDeviceSize vertex_offset=0;p_vkCmdBindVertexBuffers(cmd,7,1,&buffers[6],&vertex_offset);
          if(index_width) {
            p_vkCmdBindIndexBuffer(cmd,buffers[8],0,index_width==2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
            p_vkCmdDrawIndexed(cmd,3,10923,1,-1,5);
          } else p_vkCmdDraw(cmd,3,10923,0,5);
        } else {
        p_vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,fallback ? filled : lines);
        p_vkCmdBindIndexBuffer(cmd,buffers[fallback ? 1 : 5],0,VK_INDEX_TYPE_UINT32);
        p_vkCmdDrawIndexed(cmd,fallback ? 6 : count,1,0,0,0);
        }
      }
      if(dynamic) {
        CHECK(hybris_rendering_scope_end(&scope,cmd,end_render));
        VkImageMemoryBarrier copied={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
          .srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT,
          .oldLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
          .image=image,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
        p_vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
          0,0,NULL,0,NULL,1,&copied);
      } else p_vkCmdEndRenderPass(cmd);
      VkBufferImageCopy copy={.bufferOffset=reference*1024,.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={16,16,1}};
      p_vkCmdCopyImageToBuffer(cmd,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffers[4],1,&copy);
    }
    hybris_rendering_scope_reset(&scope);
    VkMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
    p_vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,NULL,0,NULL);
    CHECK(p_vkEndCommandBuffer(cmd));
    VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cmd};
    CHECK(p_vkQueueSubmit(queue,1,&submit,fence)); CHECK(p_vkWaitForFences(device,1,&fence,VK_TRUE,10000000000ull));
    if(restore_state) hybris_bc_command_state_reset(&journal);
    for(unsigned v=0;v<6;++v) for(unsigned j=0;j<12;++j) {
      unsigned element=instanced ? v%3 : v;
      unsigned source=index_width ? fetch_indices[element+1]-1 : element;
      float expected_record=j<4 ? positions[source][j]*transform[j]+transform[j+4] :
        j<8 ? (j==4 ? (float)((source+1)&1) : j==5 ? .5f : 1.0f) :
        j<11 ? positions[source][j-8] : 2.0f;
      if(j<4) expected_record+=resource_bias[j];
      if(instanced && j==5) expected_record+=(5+v/3)*.02f+5*.01f+(index_width ? -1.0f : 0.0f)*.001f;
      float actual=((float*)mapped[0])[v*12+j];
      if(!(actual>=expected_record-.00001f && actual<=expected_record+.00001f)) {
        printf("FAIL vertex record vertex=%u component=%u expected=%g actual=%g\n",v,j,expected_record,actual);return 1;
      }
    }
    const uint32_t expected_draw[]={count,1,0,0,0,fallback ? params[8] : 0u,params[9],params[10],params[11],params[12]};
    if(!normal_overflow && (memcmp(mapped[3],expected_draw,sizeof(expected_draw)) || memcmp(mapped[2],expected,count*4))) {
      printf("FAIL mode=%u cull=%u ccw=%u flip=%u count=%u actual=%u\n",mode,cull,ccw,flip,count,*(uint32_t*)mapped[3]); return 1;
    }
    for(unsigned j=count;!normal_overflow && !native_buffers && j<sizes[2]/4;++j) if(((uint32_t*)mapped[2])[j]!=0xcdcdcdcd) {
      printf("FAIL overwrite index=%u count=%u capacity=%u\n",j,count,capacities[cap]);return 1;
    }
    if(memcmp(mapped[4],(unsigned char*)mapped[4]+1024,1024)) {
      printf("FAIL pixels mode=%u cull=%u ccw=%u flip=%u fallback=%d\n",mode,cull,ccw,flip,fallback);return 1;
    }
    if(dynamic && ((uint32_t*)mapped[4])[0]!=0xff0000ffu) {puts("FAIL interrupted rendering lost marker");return 1;}
    unsigned lit=0; for(unsigned j=dynamic ? 1 : 0;j<256;++j) if(((uint32_t*)mapped[4])[j]) ++lit;
    int visible=count!=0;
    if(fallback) {
      visible=0;
      for(unsigned t=0;t<2;++t) {
        int triangle=instanced ? 0 : (int)t;
        int front=(ccw ? triangle==1 : triangle==0) ^ flip;
        if(!(cull & (front ? 1 : 2))) visible=1;
      }
    }
    if(visible != (lit!=0)) {puts("FAIL render visibility differs from culling");return 1;}
    p_vkDestroyPipeline(device,lines,NULL); p_vkDestroyPipeline(device,filled,NULL);
    if(normal_pipeline) p_vkDestroyPipeline(device,normal_pipeline,NULL);
    if(normal_reference) p_vkDestroyPipeline(device,normal_reference,NULL);
    hybris_polygon_bindings_destroy(companions);
    if(owned_pool) p_vkDestroyDescriptorPool(device,owned_pool,NULL);
    hybris_polygon_pipeline_destroy(owned);
    /* Cleanup owns device/allocator values, not a borrowed pipeline pointer. */
    hybris_polygon_buffers_destroy(scratch);
    filled_cases+=fallback;
    ++cases;
  }
  p_vkDestroyFence(device,fence,NULL); p_vkDestroyCommandPool(device,cp,NULL);
  p_vkDestroyPipeline(device,producer,NULL);p_vkDestroyShaderModule(device,producer_shader,NULL);
  if(restore_state) {
    hybris_bc_layout_release(&journal_layouts,application_identity);
    hybris_bc_layouts_finish(&journal_layouts);
  }
  p_vkDestroyPipelineLayout(device,producer_layout,NULL);p_vkDestroyDescriptorPool(device,producer_pool,NULL);
  p_vkDestroyDescriptorSetLayout(device,producer_sl,NULL);
  p_vkDestroyShaderModule(device,vs,NULL); p_vkDestroyShaderModule(device,fs,NULL);
  p_vkDestroyFramebuffer(device,fb,NULL); p_vkDestroyRenderPass(device,rp,NULL);
  p_vkDestroyImageView(device,view,NULL); p_vkDestroyImage(device,image,NULL); p_vkFreeMemory(device,imem,NULL);
  p_vkDestroyDescriptorPool(device,converter_pool,NULL);
  hybris_polygon_indices_destroy(&converter,NULL);
  p_vkDestroyDescriptorPool(device,pool,NULL);
  p_vkDestroyDescriptorSetLayout(device,sl,NULL);
  p_vkDestroyDescriptorPool(device,resource_pool,NULL);
  if(original_vertex) p_vkDestroyShaderModule(device,original_vertex,NULL);
  if(application_layout) p_vkDestroyPipelineLayout(device,application_layout,NULL);
  p_vkDestroyDescriptorSetLayout(device,original_resource_layout,NULL);p_vkDestroyDescriptorSetLayout(device,compute_resource_layout,NULL);
  for(unsigned i=0;i<10;++i) {p_vkUnmapMemory(device,memory[i]);p_vkDestroyBuffer(device,buffers[i],NULL);p_vkFreeMemory(device,memory[i],NULL);}
  p_vkDestroyDevice(device,NULL);
  if(messenger) destroy_messenger(instance,messenger,NULL);
  p_vkDestroyInstance(instance,NULL); dlclose(h);
  printf("GPU_POLYGON_DRAW_%s cases=%u converted=%u filled_fallback=%u validation_errors=%u\n",
    validation.errors ? "FAIL" : "PASS",cases,cases-filled_cases,filled_cases,validation.errors);
  if(normal_draw) puts("STANDARD_VULKAN_DRAW_PASS target uses only application pipeline/bindings/Draw APIs; no manual conversion helpers");
  return validation.errors ? 1 : 0;
}
