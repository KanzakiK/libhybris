/* SPDX-License-Identifier: Apache-2.0 */
#include "vertex_compute.h"
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv) {
    if(argc<3 || argc>5) return 2;
    FILE *f=fopen(argv[1],"rb");if(!f) return 2;
    if(fseek(f,0,SEEK_END)) return 2;
    long size=ftell(f);if(size<20 || size>1024*1024) return 2;
    rewind(f);uint32_t *input=malloc(size);
    if(!input || fread(input,1,size,f)!=(size_t)size) return 2;
    fclose(f);uint32_t *out=NULL;size_t bytes=0;const char *reason=NULL;
    struct hybris_vertex_compute_layout layout;
    int replay=argc==5 && !strcmp(argv[4],"--replay");
    int fetch=argc==5 && (!strcmp(argv[4],"--fetch") || !strcmp(argv[4],"--fetch-unsupported"));
    VkVertexInputBindingDescription binding={.binding=7,.stride=32,.inputRate=VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attributes[64];uint32_t attribute_count=1;
    attributes[0]=(VkVertexInputAttributeDescription){.location=0,.binding=7,.format=VK_FORMAT_R32G32B32A32_SFLOAT,.offset=8};
    if(fetch) {
        uint32_t *replay_code=NULL;size_t replay_size=0;const char *replay_reason=NULL;
        struct hybris_vertex_compute_layout plan;
        VkResult probe=hybris_spirv_vertex_replay(input,size,argc>=4 ? (uint32_t)strtoul(argv[3],NULL,10) : 0,
            &plan,NULL,&replay_code,&replay_size,&replay_reason);
        free(replay_code);
        if(probe) {fprintf(stderr,"FETCH_PLAN_FAIL %s\n",replay_reason);free(input);return 1;}
        attribute_count=plan.input_count;binding.stride=16*(attribute_count+1);
        for(unsigned i=0;i<attribute_count;++i) {
            unsigned components=plan.inputs[i].component+plan.inputs[i].components;
            unsigned kind=plan.inputs[i].kind==0 ? 2 : plan.inputs[i].kind==1 ? 0 : 1;
            attributes[i]=(VkVertexInputAttributeDescription){.location=plan.inputs[i].location,.binding=7,
                .format=(VkFormat)(VK_FORMAT_R32_UINT+3*(components-1)+kind),.offset=8+16*i};
        }
    }
    if(fetch && !strcmp(argv[4],"--fetch-unsupported")) binding.stride=30;
    VkPipelineVertexInputStateCreateInfo vi={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount=1,.pVertexBindingDescriptions=&binding,
        .vertexAttributeDescriptionCount=attribute_count,.pVertexAttributeDescriptions=attributes};
    uint32_t set=argc>=4 ? (uint32_t)strtoul(argv[3],NULL,10) : 0;
    VkResult status=fetch ? hybris_spirv_vertex_fetch_compute(input,size,set,&vi,&layout,NULL,&out,&bytes,&reason) :
        replay ? hybris_spirv_vertex_replay(input,size,set,&layout,NULL,&out,&bytes,&reason) :
        hybris_spirv_vertex_compute(input,size,set,&layout,NULL,&out,&bytes,&reason);
    if(!strcmp(argv[2],"--expect-unsupported")) {
        free(input);
        if(status!=VK_ERROR_FEATURE_NOT_PRESENT || out || bytes) {free(out);return 1;}
        printf("VERTEX_COMPUTE_REJECT_PASS reason=%s\n",reason);return 0;
    }
    if(status) {fprintf(stderr,"VERTEX_COMPUTE_FAIL status=%d reason=%s\n",status,reason);return 1;}
    f=fopen(argv[2],"wb");if(!f || fwrite(out,1,bytes,f)!=bytes) return 2;
    fclose(f);free(input);free(out);
    printf("VERTEX_COMPUTE_LOWER_PASS bytes=%zu inputs=%u stride=%u outputs=%u stride=%u\n",bytes,
        layout.input_count,layout.input_stride,layout.output_count,layout.output_stride);return 0;
}
