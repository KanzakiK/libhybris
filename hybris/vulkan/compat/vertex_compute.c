/* SPDX-License-Identifier: Apache-2.0 */
#include "vertex_compute.h"
#include "spirv_builtins.h"
#include <stdarg.h>
#include <string.h>
struct definition { size_t at; uint32_t type, op, alias, location, component, builtin, storage_ptr, main_local; };
struct builder { uint32_t *code; size_t used, capacity; };
static const unsigned char result_positions[]={
#include "spirv_results.inc"
};
static int emit(struct builder *b,unsigned op,unsigned count,...) {
    if(count>b->capacity-b->used) return 0;
    b->code[b->used++]=(count<<16)|op;
    va_list args;va_start(args,count);
    for(unsigned j=1;j<count;++j) b->code[b->used++]=va_arg(args,uint32_t);
    va_end(args);return 1;
}
static int emit_struct(struct builder *b,uint32_t id,const uint32_t *types,unsigned count) {
    if(count+2>b->capacity-b->used) return 0;
    b->code[b->used++]=((count+2)<<16)|30;b->code[b->used++]=id;
    memcpy(b->code+b->used,types,count*4);b->used+=count;return 1;
}
static int field_type(const uint32_t *w,struct definition *ids,uint32_t type,
    struct hybris_vertex_compute_field *field) {
    const uint32_t *p=w+ids[type].at;field->components=1;
    if(ids[type].op==23) {if(p[3]<2 || p[3]>4) return 0;field->components=p[3];type=p[2];p=w+ids[type].at;}
    if(ids[type].op==22 && p[2]==32) field->kind=0;
    else if(ids[type].op==21 && p[2]==32) field->kind=p[3] ? 2 : 1;
    else return 0;
    return 1;
}
static VkResult transform_vertex(const uint32_t *w,size_t bytes,uint32_t set,int replay,
    const VkPipelineVertexInputStateCreateInfo *fetch,
    struct hybris_vertex_compute_layout *layout,
    const VkAllocationCallbacks *alloc,uint32_t **output,size_t *output_size,const char **reason) {
    *output=NULL;*output_size=0;*reason="unsupported vertex compute interface";
    if(layout) memset(layout,0,sizeof(*layout));
    if(!w || bytes<20 || bytes%4 || w[0]!=0x07230203 || w[1]>0x00010600 || !w[3] || w[3]>1000000)
        return VK_ERROR_FEATURE_NOT_PRESENT;
    size_t total=bytes/4; uint32_t bound=w[3];
    const int all_globals=w[1]>=0x00010400;
    const uint32_t storage=w[1]>=0x00010300 ? 12u : 2u;
    const uint32_t block=storage==12 ? 2u : 3u;
    struct definition *ids=hybris_scaled_alloc(alloc,bound*sizeof(*ids),VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
    if(!ids) return VK_ERROR_OUT_OF_HOST_MEMORY;
    memset(ids,0,bound*sizeof(*ids));
    for(uint32_t i=0;i<bound;++i) ids[i].location=ids[i].builtin=UINT32_MAX;
    uint32_t entry=0, position=0, vertex_index=0, instance_index=0, base_vertex=0, base_instance=0,
        position_member=UINT32_MAX;
    uint32_t inputs[64],outputs[65]={0},intypes[64],outtypes[65],in_count=0,out_count=1;
    struct hybris_vertex_compute_layout plan={0};
    uint32_t u32=0, f32=0, v4=0, private_v4=0, next=bound;
    unsigned entries=0; uint32_t scan_function=0; VkResult result=VK_ERROR_FEATURE_NOT_PRESENT;
    struct builder b={0};
    for(size_t at=5;at<total;) {
        const uint32_t *p=w+at;unsigned n=p[0]>>16,op=p[0]&65535;
        if(!n || n>total-at) goto done;
        if(op==54 && n>=3) scan_function=p[2];
        if(scan_function==entry && op!=54 && op<sizeof(result_positions) && result_positions[op]>1) {
            unsigned word=result_positions[op]-1;
            if(word>=n || p[word]>=bound) goto done;
            ids[p[word]].main_local=1;
        }
        if(op==56) scan_function=0;
        if(op<sizeof(result_positions) && result_positions[op]==3) {
            if(n<3 || p[1]>=bound || p[2]>=bound) goto done;
            ids[p[2]].type=p[1];
        }
        // Cross-invocation effects cannot be replayed as a different execution
        // model. Keep the original filled draw rather than repeating them.
        if(op==99 || op==224 || op==225 || (op>=227 && op<=242) ||
           op==318 || op==319 || op==5614 || op==5615 || op==6035 ||
           op==6142 || op==6143) {
            *reason="vertex shader has externally visible effects or synchronization";
            goto done;
        }
        if(op>=333 && op<=366) {
            *reason="vertex subgroup operations cannot preserve the original execution grouping";
            goto done;
        }
        if(op==15) {if(n<5 || p[1]!=0 || ++entries!=1 || p[2]>=bound) goto done;entry=p[2];}
        if(op==73 || op==74 || op==75 || op==332) goto done;
        // Preserve floating-point controls; reject vertex-only modes instead
        // of silently copying them into a compute execution model.
        if(op==16 && (n<3 || p[2]<4459 || p[2]>4463)) goto done;
        if(op==331 && (n<3 || p[2]!=6028)) goto done;
        if(op==71 && n>=4) {
            if(p[1]>=bound) goto done;
            if(p[2]==34 && p[3]==set) {*reason="reserved descriptor set already used";goto done;}
            if(p[2]==30) ids[p[1]].location=p[3];
            if(p[2]==11) ids[p[1]].builtin=p[3];
            if(p[2]==31) {if(p[3]>3) goto done;ids[p[1]].component=p[3];}
        }
        if(op>=19 && op<=33) {
            if(n<2 || p[1]>=bound) goto done;
            ids[p[1]].at=at;ids[p[1]].op=op;
            if(op==21 && n==4 && p[2]==32 && !p[3]) u32=p[1];
            if(op==22 && n==3 && p[2]==32) f32=p[1];
        }
        if(op==59) {
            if(n!=4 || p[1]>=bound || p[2]>=bound) goto done;
            ids[p[2]].at=at;ids[p[2]].op=op;ids[p[2]].type=p[1];
        }
        at+=n;
    }
    if(entries!=1 || !f32) goto done;
    for(size_t at=5;at<total;at+=w[at]>>16) {
        const uint32_t *p=w+at;unsigned op=p[0]&65535;
        if(op!=62 && op!=63 && op!=64) continue;
        if((p[0]>>16)<3 || p[1]>=bound) goto done;
        uint32_t type=ids[p[1]].type;
        if(!type || ids[type].op!=32) goto done;
        uint32_t target_storage=w[ids[type].at+2];
        if(target_storage!=3 && target_storage!=6 && target_storage!=7) {
            *reason="vertex shader writes an external resource";
            goto done;
        }
    }
    for(uint32_t i=1;i<bound;++i) if(ids[i].op==23) {
        const uint32_t *p=w+ids[i].at;if(p[2]==f32 && p[3]==4) v4=i;
    }
    if(!v4) goto done;
    // Canonicalize interface pointers before changing their storage class.
    for(uint32_t i=1;i<bound;++i) if(ids[i].op==32) {
        const uint32_t *p=w+ids[i].at;
        if(p[2]!=1 && p[2]!=6 && (replay || p[2]!=3)) continue;
        if(replay && p[2]==3) continue;
        for(uint32_t j=1;j<i;++j) if(ids[j].op==32 && !ids[j].alias) {
            const uint32_t *q=w+ids[j].at;
            if((q[2]==1 || (!replay && q[2]==3) || q[2]==6) && q[3]==p[3]) {ids[i].alias=j;break;}
        }
        if(p[3]==v4) private_v4=ids[i].alias ? ids[i].alias : i;
    }
    if(replay) for(uint32_t i=1;i<bound;++i) if(ids[i].op==32) {
        const uint32_t *p=w+ids[i].at;
        if(p[2]==3 && p[3]==v4) private_v4=i;
    }
    for(uint32_t i=1;i<bound;++i) if(ids[i].op==59) {
        const uint32_t *p=w+ids[i].at;
        if(p[3]!=1 && p[3]!=3) continue;
        uint32_t type=ids[i].type;
        if(ids[type].op!=32) goto done;
        uint32_t value=w[ids[type].at+3];
        if(p[3]==1) {
            if(ids[i].location!=UINT32_MAX && in_count<64) inputs[in_count++]=i;
            else if(ids[i].builtin==42 && ids[value].op==21 && w[ids[value].at+2]==32 && !vertex_index) vertex_index=i;
            else if((fetch || replay) && ids[value].op==21 && w[ids[value].at+2]==32) {
                if(ids[i].builtin==43 && !instance_index) instance_index=i;
                else if(ids[i].builtin==4424 && !base_vertex) base_vertex=i;
                else if(ids[i].builtin==4425 && !base_instance) base_instance=i;
                else goto done;
            }
            else goto done;
        } else if(ids[i].location!=UINT32_MAX && out_count<65) outputs[out_count++]=i;
        else if(ids[i].builtin==0 && value==v4 && !position) position=i;
        else if(ids[value].op==30 && (w[ids[value].at]>>16)==3 && w[ids[value].at+2]==v4 && !position) {
            int found=0;
            for(size_t at=5;at<total;at+=w[at]>>16) {
                const uint32_t *q=w+at;
                if((q[0]&65535)==72 && (q[0]>>16)==5 && q[1]==value && q[2]==0 && q[3]==11 && q[4]==0) found=1;
            }
            if(!found) goto done;
            position=i;position_member=0;
        } else goto done;
    }
    if(!position || !private_v4) goto done;
    outputs[0]=position;
    for(unsigned i=0;i<in_count;++i) for(unsigned j=i+1;j<in_count;++j)
        if(ids[inputs[j]].location<ids[inputs[i]].location ||
           (ids[inputs[j]].location==ids[inputs[i]].location && ids[inputs[j]].component<ids[inputs[i]].component))
            {uint32_t swap=inputs[i];inputs[i]=inputs[j];inputs[j]=swap;}
    for(unsigned i=1;i<out_count;++i) for(unsigned j=i+1;j<out_count;++j)
        if(ids[outputs[j]].location<ids[outputs[i]].location ||
           (ids[outputs[j]].location==ids[outputs[i]].location && ids[outputs[j]].component<ids[outputs[i]].component))
            {uint32_t swap=outputs[i];outputs[i]=outputs[j];outputs[j]=swap;}
    for(unsigned i=0;i<in_count;++i) {
        intypes[i]=w[ids[ids[inputs[i]].type].at+3];
        plan.inputs[i].location=ids[inputs[i]].location;plan.inputs[i].offset=16*i;
        plan.inputs[i].component=ids[inputs[i]].component;
        if(!field_type(w,ids,intypes[i],&plan.inputs[i]) || plan.inputs[i].component+plan.inputs[i].components>4 ||
           (i && plan.inputs[i].location==plan.inputs[i-1].location &&
            plan.inputs[i].component<plan.inputs[i-1].component+plan.inputs[i-1].components)) goto done;
    }
    for(unsigned i=0;i<out_count;++i) {
        outtypes[i]=i ? w[ids[ids[outputs[i]].type].at+3] : v4;
        plan.outputs[i].location=i ? ids[outputs[i]].location : UINT32_MAX;plan.outputs[i].offset=16*i;
        plan.outputs[i].component=i ? ids[outputs[i]].component : 0;
        if(!field_type(w,ids,outtypes[i],&plan.outputs[i]) || plan.outputs[i].component+plan.outputs[i].components>4 ||
           (i>1 && plan.outputs[i].location==plan.outputs[i-1].location &&
            plan.outputs[i].component<plan.outputs[i-1].component+plan.outputs[i-1].components)) goto done;
    }
    plan.input_count=in_count;plan.output_count=out_count;plan.input_stride=16*in_count;plan.output_stride=16*out_count;
    uint32_t strides[64]={0}, offsets[64]={0}, instance_rates[64]={0};
    if(fetch) {
        if(fetch->pNext || (fetch->vertexAttributeDescriptionCount && !fetch->pVertexAttributeDescriptions) ||
           (fetch->vertexBindingDescriptionCount && !fetch->pVertexBindingDescriptions)) goto done;
        for(unsigned i=0;i<in_count;++i) {
            const VkVertexInputAttributeDescription *attribute=NULL;
            const VkVertexInputBindingDescription *binding=NULL;
            for(unsigned j=0;j<fetch->vertexAttributeDescriptionCount;++j)
                if(fetch->pVertexAttributeDescriptions[j].location==plan.inputs[i].location) {
                    if(attribute) goto done;
                    attribute=&fetch->pVertexAttributeDescriptions[j];
                }
            if(!attribute) goto done;
            for(unsigned j=0;j<fetch->vertexBindingDescriptionCount;++j)
                if(fetch->pVertexBindingDescriptions[j].binding==attribute->binding) {
                    if(binding) goto done;
                    binding=&fetch->pVertexBindingDescriptions[j];
                }
            if(!binding || (binding->inputRate!=VK_VERTEX_INPUT_RATE_VERTEX && binding->inputRate!=VK_VERTEX_INPUT_RATE_INSTANCE) || binding->stride%4 ||
               attribute->offset%4 || attribute->offset>UINT32_MAX-16 ||
               attribute->format<VK_FORMAT_R32_UINT || attribute->format>VK_FORMAT_R32G32B32A32_SFLOAT) {
                *reason="unsupported vertex fetch format, alignment or rate";goto done;
            }
            unsigned format=(unsigned)attribute->format-VK_FORMAT_R32_UINT;
            unsigned kind=format%3==2 ? 0 : format%3==0 ? 1 : 2;
            if(kind!=plan.inputs[i].kind || format/3+1<plan.inputs[i].component+plan.inputs[i].components) {
                *reason="vertex fetch format does not supply shader components";goto done;
            }
            strides[i]=binding->stride/4;offsets[i]=attribute->offset/4+plan.inputs[i].component;
            instance_rates[i]=binding->inputRate==VK_VERTEX_INPUT_RATE_INSTANCE;
        }
    }
    uint32_t new_uint=u32 ? 0 : next++; if(!u32) u32=new_uint;
    uint32_t constants[65];for(unsigned i=0;i<65;++i) constants[i]=next++;
    uint32_t zero=constants[0], uv3=next++, gidptr=next++, gid=next++;
    uint32_t inrecord=next++;
    uint32_t inarray=next++, inblock=next++, inptr=next++, invar=next++;
    uint32_t record=next++, outarray=next++, outblock=next++, outptr=next++, outvar=next++;
    uint32_t invocation=next++, index=next++;
    uint32_t rawarray=0,rawblock=0,rawptr=0,wordptr=0,indexvar=0,boolean=0,mask16=0,new_boolean=0;
    uint32_t vertex=index,instance=0,first_vertex=0,first_instance=0,base=0;
    uint32_t sources[64]={0},stride_constants[64]={0},offset_constants[64]={0};
    if(fetch) {
        rawarray=next++;rawblock=next++;rawptr=next++;wordptr=next++;
        indexvar=next++;mask16=next++;
        for(uint32_t i=1;i<bound;++i) if(ids[i].op==20) boolean=i;
        if(!boolean) {boolean=next++;new_boolean=1;}
        vertex=next++;instance=next++;first_vertex=next++;first_instance=next++;base=next++;
        for(unsigned i=0;i<in_count;++i) {sources[i]=next++;stride_constants[i]=next++;offset_constants[i]=next++;}
    }
    uint32_t pointptr=0,pointvar=0,pointone=0;
    if(replay) {
        for(uint32_t i=1;i<bound;++i) if(ids[i].op==32) {
            const uint32_t *p=w+ids[i].at;if(p[2]==3 && p[3]==f32) pointptr=i;
        }
        if(!pointptr) pointptr=next++;
        pointvar=next++;pointone=next++;
    }
    for(unsigned i=0;i<in_count;++i) if(!ids[intypes[i]].storage_ptr) ids[intypes[i]].storage_ptr=next++;
    for(unsigned i=0;i<out_count;++i) if(!ids[outtypes[i]].storage_ptr) ids[outtypes[i]].storage_ptr=next++;
    if(total>(SIZE_MAX/4-8192)/3) {result=VK_ERROR_OUT_OF_HOST_MEMORY;goto done;}
    b.capacity=total*3+8192;
    b.code=hybris_scaled_alloc(alloc,b.capacity*4,VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
    if(!b.code) {result=VK_ERROR_OUT_OF_HOST_MEMORY;goto done;}
    memcpy(b.code,w,20);b.used=5;
    unsigned annotations=0, declarations=0, prefix=0;uint32_t function=0;
#define E(...) do {if(!emit(&b,__VA_ARGS__)) {result=VK_ERROR_OUT_OF_HOST_MEMORY;goto done;}} while(0)
    for(size_t at=5;at<total;at+=w[at]>>16) {
        const uint32_t *p=w+at;unsigned n=p[0]>>16,op=p[0]&65535;
        if(replay && (op==5 || op==6 || op==71 || op==72) && n>1 &&
           p[1]<bound && ids[p[1]].main_local) continue;
        if(replay && function==entry && op!=54) {if(op==56) function=0;continue;}
        if(op==17 && n==2 && p[1]==4427) continue; /* DrawParameters inputs are now private. */
        if(!annotations && op>=19 && op<=33) {
            E(71,4,gid,11u,replay ? 42u : 28u);
            if(replay) E(71,4,pointvar,11u,1u);
            E(71,4,inarray,6u,16u*(in_count ? in_count : 1u));E(71,3,inblock,block);E(72,5,inblock,0u,35u,0u);
            for(unsigned i=0;i<(in_count ? in_count : 1u);++i) E(72,5,inrecord,i,35u,16u*i);
            E(71,4,invar,34u,set);E(71,4,invar,33u,0u);
            E(71,3,invar,24u); /* NonWritable: vertex records are always read-only. */
            for(unsigned i=0;i<out_count;++i) E(72,5,record,i,35u,16u*i);
            E(71,4,outarray,6u,16u*out_count);E(71,3,outblock,block);E(72,5,outblock,0u,35u,0u);
            E(71,4,outvar,34u,set);E(71,4,outvar,33u,1u);
            if(replay) E(71,3,outvar,24u); /* Replay reads compute-produced records. */
            if(fetch) {
                E(71,4,rawarray,6u,4u);E(71,3,rawblock,block);E(72,5,rawblock,0u,35u,0u);
                for(unsigned i=0;i<in_count;++i) {
                    E(71,4,sources[i],34u,set);E(71,4,sources[i],33u,2u+i);E(71,3,sources[i],24u);
                }
                E(71,4,indexvar,34u,set);E(71,4,indexvar,33u,2u+in_count);E(71,3,indexvar,24u);
            }
            annotations=1;
        }
        if(op==15) {
            const char *name=(const char*)(p+3);
            const char *end=memchr(name,0,(n-3)*4);if(!end) goto done;
            unsigned names=(unsigned)(end-name)/4+1;
            unsigned count=all_globals ? n+(replay ? 4u : 3u) : replay ? 5+names+out_count : 4+names;
            if(all_globals && fetch) count+=in_count+1;
            if(count>65535 || count>b.capacity-b.used) {result=VK_ERROR_OUT_OF_HOST_MEMORY;goto done;}
            b.code[b.used++]=(count<<16)|15;b.code[b.used++]=replay ? 0 : 5;b.code[b.used++]=entry;
            unsigned tail=all_globals ? n-3 : names;
            memcpy(b.code+b.used,p+3,tail*4);b.used+=tail;b.code[b.used++]=gid;
            if(all_globals) {b.code[b.used++]=invar;b.code[b.used++]=outvar;}
            else if(replay) for(unsigned i=0;i<out_count;++i) b.code[b.used++]=outputs[i];
            if(all_globals && fetch) for(unsigned i=0;i<in_count;++i) b.code[b.used++]=sources[i];
            if(all_globals && fetch) b.code[b.used++]=indexvar;
            if(replay) b.code[b.used++]=pointvar;
            else E(16,6,entry,17u,1u,1u,1u);
            continue;
        }
        if(op==71 && n>=3 && p[1]<bound) {
            if((ids[p[1]].op==59 && (w[ids[p[1]].at+3]==1 || (!replay && w[ids[p[1]].at+3]==3))) ||
                (!replay && p[2]==2 && p[1]==w[ids[ids[position].type].at+3])) continue;
        }
        if(!replay && op==72 && n>=4 && p[1]<bound && p[3]==11) continue;
        if(op==32 && ids[p[1]].alias) continue;
        if(op==54 && !declarations) {
            if(new_uint) E(21,4,u32,32u,0u);
            for(unsigned i=0;i<65;++i) E(43,4,u32,constants[i],i);
            E(23,4,uv3,u32,3u);
            E(32,4,gidptr,1u,replay ? u32 : uv3);E(59,4,gidptr,gid,1u);
            if(!emit_struct(&b,inrecord,in_count ? intypes : &v4,in_count ? in_count : 1u) || !emit_struct(&b,record,outtypes,out_count)) {result=VK_ERROR_OUT_OF_HOST_MEMORY;goto done;}
            E(29,3,inarray,inrecord);E(30,3,inblock,inarray);E(32,4,inptr,storage,inblock);
            if(!fetch) E(59,4,inptr,invar,storage);
            E(29,3,outarray,record);E(30,3,outblock,outarray);
            E(32,4,outptr,storage,outblock);E(59,4,outptr,outvar,storage);
            if(fetch) {
                E(29,3,rawarray,u32);E(30,3,rawblock,rawarray);
                E(32,4,rawptr,storage,rawblock);E(32,4,wordptr,storage,u32);
                if(new_boolean) E(20,2,boolean);
                E(43,4,u32,mask16,65535u);
                E(59,4,rawptr,invar,storage);E(59,4,rawptr,indexvar,storage);
                for(unsigned i=0;i<in_count;++i) {
                    E(59,4,rawptr,sources[i],storage);
                    E(43,4,u32,stride_constants[i],strides[i]);E(43,4,u32,offset_constants[i],offsets[i]);
                }
            }
            for(uint32_t i=1;i<bound;++i) if(ids[i].storage_ptr) E(32,4,ids[i].storage_ptr,storage,i);
            if(replay) {
                if(pointptr>=bound) E(32,4,pointptr,3u,f32);
                E(59,4,pointptr,pointvar,3u);E(43,4,f32,pointone,0x3f800000u);
            }
            declarations=1;
        }
        if(op==54) {function=p[2];prefix=0;}
        if(replay && function==entry && op==54) {
            if(n!=5) goto done;
            E(54,5,p[1],p[2],p[3],p[4]);E(248,2,next++);
            E(61,4,u32,index,gid);
            for(unsigned i=0;i<out_count;++i) {
                uint32_t ptr=next++, value=next++, dest=outputs[i];
                E(65,7,ids[outtypes[i]].storage_ptr,ptr,outvar,zero,index,constants[i]);E(61,4,outtypes[i],value,ptr);
                if(!i && position_member!=UINT32_MAX) {dest=next++;E(65,5,private_v4,dest,position,zero);}
                E(62,3,dest,value);
            }
            E(62,3,pointvar,pointone);E(253,1);E(56,1);continue;
        }
        if(function==entry && !prefix && op!=54 && op!=248 && op!=59) {
            E(61,4,uv3,invocation,gid);E(81,5,u32,index,invocation,0u);
            if(fetch) {
                /* Draw parameters: firstVertex, firstIndex, vertexOffset,
                 * firstInstance, verticesPerInstance, indexWidth (0/2/4).
                 * Output record index stays the invocation ordinal. */
                uint32_t params[6]={first_vertex,next++,next++,first_instance,next++,next++};
                for(unsigned i=0;i<6;++i) {
                    uint32_t ptr=next++;E(65,6,wordptr,ptr,invar,zero,constants[i]);E(61,4,u32,params[i],ptr);
                }
                uint32_t element=next++,relative_instance=next++,direct=next++,is_direct=next++,is16=next++;
                E(137,5,u32,element,index,params[4]);E(134,5,u32,relative_instance,index,params[4]);
                E(128,5,u32,instance,relative_instance,first_instance);
                E(128,5,u32,direct,element,first_vertex);
                E(170,5,boolean,is_direct,params[5],zero);E(170,5,boolean,is16,params[5],constants[2]);
                uint32_t direct_label=next++,indexed_label=next++,merge=next++;
                E(247,3,merge,0u);E(250,4,is_direct,direct_label,indexed_label);
                E(248,2,direct_label);E(249,2,merge);E(248,2,indexed_label);
                uint32_t source_index=next++,half=next++,word=next++,ptr=next++,bits=next++,odd=next++,shift=next++,shifted=next++,short_index=next++,value=next++,indexed=next++;
                E(128,5,u32,source_index,element,params[1]);E(194,5,u32,half,source_index,constants[1]);
                E(169,6,u32,word,is16,half,source_index);
                E(65,6,wordptr,ptr,indexvar,zero,word);E(61,4,u32,bits,ptr);
                E(199,5,u32,odd,source_index,constants[1]);E(132,5,u32,shift,odd,constants[16]);
                E(194,5,u32,shifted,bits,shift);E(199,5,u32,short_index,shifted,mask16);
                E(169,6,u32,value,is16,short_index,bits);E(128,5,u32,indexed,value,params[2]);
                E(249,2,merge);E(248,2,merge);E(245,7,u32,vertex,direct,direct_label,indexed,indexed_label);
                E(169,6,u32,base,is_direct,first_vertex,params[2]);
            }
            for(unsigned i=0;i<in_count;++i) {
                if(fetch) {
                    uint32_t product=next++,base=next++,values[4];
                    uint32_t scalar=ids[intypes[i]].op==23 ? w[ids[intypes[i]].at+2] : intypes[i];
                    E(132,5,u32,product,instance_rates[i] ? instance : vertex,stride_constants[i]);E(128,5,u32,base,product,offset_constants[i]);
                    for(unsigned j=0;j<plan.inputs[i].components;++j) {
                        uint32_t address=base,ptr=next++,bits=next++;
                        if(j) {address=next++;E(128,5,u32,address,base,constants[j]);}
                        E(65,6,wordptr,ptr,sources[i],zero,address);E(61,4,u32,bits,ptr);
                        values[j]=bits;
                        if(scalar!=u32) {values[j]=next++;E(124,4,scalar,values[j],bits);}
                    }
                    uint32_t value=values[0];
                    if(plan.inputs[i].components>1) {
                        value=next++;
                        unsigned count=plan.inputs[i].components;
                        if(count+3>b.capacity-b.used) {result=VK_ERROR_OUT_OF_HOST_MEMORY;goto done;}
                        b.code[b.used++]=((count+3)<<16)|80;
                        b.code[b.used++]=intypes[i];b.code[b.used++]=value;
                        memcpy(b.code+b.used,values,count*4);b.used+=count;
                    }
                    E(62,3,inputs[i],value);
                } else {
                    uint32_t ptr=next++, value=next++;
                    E(65,7,ids[intypes[i]].storage_ptr,ptr,invar,zero,index,constants[i]);
                    E(61,4,intypes[i],value,ptr);E(62,3,inputs[i],value);
                }
            }
            if(vertex_index) {
                uint32_t type=w[ids[ids[vertex_index].type].at+3], cast=next++;
                E(124,4,type,cast,vertex);E(62,3,vertex_index,cast);
            }
            uint32_t builtins[3]={instance_index,base_vertex,base_instance},values[3]={instance,base,first_instance};
            for(unsigned i=0;i<3;++i) if(builtins[i]) {
                uint32_t type=w[ids[ids[builtins[i]].type].at+3],cast=next++;
                E(124,4,type,cast,values[i]);E(62,3,builtins[i],cast);
            }
            prefix=1;
        }
        if(function==entry && op==253) {
            uint32_t position_ptr=position;
            if(position_member!=UINT32_MAX) {position_ptr=next++;E(65,5,private_v4,position_ptr,position,zero);}
            for(unsigned i=0;i<out_count;++i) {
                uint32_t value=next++, ptr=next++;
                E(61,4,outtypes[i],value,i ? outputs[i] : position_ptr);
                E(65,7,ids[outtypes[i]].storage_ptr,ptr,outvar,zero,index,constants[i]);E(62,3,ptr,value);
            }
        }
        if(b.used+n>b.capacity) {result=VK_ERROR_OUT_OF_HOST_MEMORY;goto done;}
        uint32_t *q=b.code+b.used;memcpy(q,p,n*4);
        // Alias only type operands, never decoration offsets, enum payloads,
        // string words or other literals that happen to equal a pointer ID.
        if((op<sizeof(result_positions) && result_positions[op]==3) || op==5 || op==71 || op==72)
            if(n>1 && q[1]<bound && ids[q[1]].alias) q[1]=ids[q[1]].alias;
        if(op==33) for(unsigned j=2;j<n;++j) if(q[j]<bound && ids[q[j]].alias) q[j]=ids[q[j]].alias;
        if(op==32 && q[3]<bound && ids[q[3]].alias) q[3]=ids[q[3]].alias;
        if(op==32 && (p[2]==1 || (!replay && p[2]==3))) q[2]=6;
        if(op==59 && (p[3]==1 || (!replay && p[3]==3))) q[3]=6;
        b.used+=n;if(op==56) function=0;
    }
    if(!declarations || !annotations) goto done;
    b.code[3]=next;*output=b.code;*output_size=b.used*4;b.code=NULL;*reason=NULL;if(layout) *layout=plan;result=VK_SUCCESS;
done:
    hybris_scaled_free(alloc,b.code);hybris_scaled_free(alloc,ids);return result;
#undef E
}
static VkResult prepare_vertex(const uint32_t *w,size_t bytes,uint32_t set,int replay,
    const VkPipelineVertexInputStateCreateInfo *fetch,
    struct hybris_vertex_compute_layout *layout,const VkAllocationCallbacks *alloc,
    uint32_t **out,size_t *size,const char **reason) {
    uint32_t *clean=NULL,*points=NULL;size_t clean_size=0,point_size=0;unsigned removed=0;
    *out=NULL;*size=0;*reason="vertex interface cleanup failed";
    if(layout) memset(layout,0,sizeof(*layout));
    VkResult result=hybris_spirv_unused_builtins(w,bytes,alloc,&clean,&clean_size,&removed);
    if(result) goto done;
    if(clean) {w=clean;bytes=clean_size;}
    result=hybris_spirv_unused_point_size(w,bytes,alloc,&points,&point_size,&removed);
    if(result) goto done;
    if(points) {w=points;bytes=point_size;}
    result=transform_vertex(w,bytes,set,replay,fetch,layout,alloc,out,size,reason);
done:
    hybris_scaled_free(alloc,points);hybris_scaled_free(alloc,clean);
    return result;
}
VkResult hybris_spirv_vertex_compute(const uint32_t *w,size_t bytes,uint32_t set,
    struct hybris_vertex_compute_layout *layout,const VkAllocationCallbacks *alloc,
    uint32_t **out,size_t *size,const char **reason) {
    return prepare_vertex(w,bytes,set,0,NULL,layout,alloc,out,size,reason);
}
VkResult hybris_spirv_vertex_replay(const uint32_t *w,size_t bytes,uint32_t set,
    struct hybris_vertex_compute_layout *layout,const VkAllocationCallbacks *alloc,
    uint32_t **out,size_t *size,const char **reason) {
    return prepare_vertex(w,bytes,set,1,NULL,layout,alloc,out,size,reason);
}
VkResult hybris_spirv_vertex_fetch_compute(const uint32_t *w,size_t bytes,uint32_t set,
    const VkPipelineVertexInputStateCreateInfo *fetch,
    struct hybris_vertex_compute_layout *layout,const VkAllocationCallbacks *alloc,
    uint32_t **out,size_t *size,const char **reason) {
    if(!fetch) {*out=NULL;*size=0;*reason="vertex input state is required";
        if(layout) memset(layout,0,sizeof(*layout));
        return VK_ERROR_FEATURE_NOT_PRESENT;}
    return prepare_vertex(w,bytes,set,0,fetch,layout,alloc,out,size,reason);
}
