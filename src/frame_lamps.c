#include "frame_lamps.h"
#include "frame_file.h"
#include "frame_lap_events.h"
#include "frame_text.h"
#include "physics_memory.h"
static uint32_t object_of(physics_memory *m,uint32_t id)
{ uint32_t r=pm_load(m,pm_load(m,0x55c694u,4)+id*8u,4);return r ? pm_load(m,r+4u,4) : 0; }
#define LAMP_TRY(x) do { if (m.error) return m.error;status=(x);if (status) return status; } while (0)
int frame_lamps_construct(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t parser,const frame_entity_factory_services *e,const frame_object_parameters_services *s)
{
    physics_memory m={read,write,ctx,0};frame_file file;const unsigned char *bytes=0;uint32_t length=0,found=0,o,unused,parent,count,i,j,p,value=0,bits=0,defined_value=0,defined_bits=0,head,mode,token;int status;
    if (!read || !write || !e || !e->work_limit || !s) return -1;
    o=object_of(&m,entity);parent=pm_load(&m,0x5466e4u,4);LAMP_TRY(frame_class_dispatch_payload(read,ctx,parent,entity,parser,e->method,e->method_ctx,&unused));
    if (!s->shapes || !s->shapes->fetch || !s->material) return -5;
    LAMP_TRY(s->shapes->fetch(s->shapes->file_ctx,pm_load(&m,0x55ae40u,4),(const unsigned char *)"lamps.dat",10,&bytes,&length,&found));if (!found) return -4;
    file.bytes=bytes;file.length=length;file.cursor=0;file.work_limit=e->work_limit;
    count=pm_load(&m,o+0x78u,4);LAMP_TRY(frame_file_integer(&file,&count,&found));if (found) pm_store(&m,o+0x78u,count,4);
    for (i=0;i<pm_load(&m,o+0x78u,4);i++) {
        if (i>=e->work_limit) return -3;
        p=0x53e578u+i*24u;
        for (j=0;j<3u;j++) { LAMP_TRY(frame_file_real32(&file,&bits,&found));if (found) defined_bits=1;if (!defined_bits) return -4;pm_store(&m,p+j*4u,frame_text_fixed1024(bits),4); }
        for (j=0;j<4u;j++) { LAMP_TRY(frame_file_integer(&file,&value,&found));if (found) defined_value=1;if (!defined_value) return -4;pm_store(&m,p+(j==3u ? 16u : 12u+j),value,1); }
        if (pm_load(&m,p+16u,1)==2u) {
            for (j=0;j<3u;j++) { LAMP_TRY(frame_file_integer(&file,&value,&found));pm_store(&m,p+(j==2u ? 17u : 20u+j*2u),value,j==2u ? 1u : 2u); }
        }
        pm_store(&m,p+15u,0,1);
    }
    mode=pm_load(&m,0x5964d4u,4)&0x8000u;LAMP_TRY(s->material(s->material_ctx,(const unsigned char *)(mode ? "glow" : "pcglow"),mode ? 5u : 7u,1,&token));pm_store(&m,o+0x7cu,token,4);
    pm_store(&m,o+0x94u,1024,4);pm_store(&m,o+0x98u,1024,4);pm_store(&m,o+0x8cu,1,4);pm_store(&m,o+0x9du,0,1);pm_store(&m,o+0x9cu,0,1);pm_store(&m,o+0x9fu,63,1);pm_store(&m,o+0x9eu,63,1);pm_store(&m,o+0xa4u,0,2);pm_store(&m,o+0x90u,(uint32_t)-10,4);
    head=pm_load(&m,0x54b48cu,4);LAMP_TRY(frame_list_remove_value(read,write,ctx,entity,head,e->release,e->system_ctx,e->work_limit));head=pm_load(&m,0x54b488u,4);LAMP_TRY(frame_list_append(read,write,ctx,entity,head,e->allocate,e->release,e->system_ctx,e->work_limit,&unused));
    mode=pm_load(&m,0x52ee9cu,4);pm_store(&m,o+0xa6u,mode==2u || mode==3u ? 0x413930u : 0x4138c0u,4);pm_store(&m,o+0xaau,mode==2u || mode==3u ? 0x413760u : 0x4137d0u,4);return m.error;
}
int frame_lamps_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t limit)
{
    physics_memory m={read,write,ctx,0};uint32_t o,callback,i,p,count,near,alternate;int32_t dt;
    int status;if (!read || !write || !limit) return -1;
    o=object_of(&m,entity);callback=pm_load(&m,o+0xaau,4);
    if (callback!=0x413760u && callback!=0x4137d0u) return m.error ? m.error : -5;
    if ((entity^pm_load(&m,0x590564u,4))&7u) return m.error;
    count=pm_load(&m,o+0x78u,4);
    for (i=0;i<count;i++) {
        if (i>=limit) return -3;
        p=0x53e578u+i*24u;
        if (callback==0x413760u) {
            pm_store(&m,p+15u,0,1);LAMP_TRY(frame_entity_near_camera(read,ctx,p,0,102400,&near));if (near) pm_store(&m,p+15u,1,1);
            LAMP_TRY(frame_entity_near_camera(read,ctx,p,1,102400,&near));if (near) pm_store(&m,p+15u,pm_load(&m,p+15u,1)|2u,1);
        } else {
            LAMP_TRY(frame_entity_near_camera(read,ctx,p,0,102400,&near));
            if (!near) pm_store(&m,p+15u,0,1);
            else if (!pm_load(&m,p+16u,1)) pm_store(&m,p+15u,1,1);
            else if (pm_load(&m,p+16u,1)==2u) {
                alternate=pm_load(&m,p+17u,1);pm_store(&m,p+15u,alternate,1);dt=pm_i32(&m,0x5904fcu);
                if (pm_i16(&m,p+18u)<=dt) { pm_store(&m,p+18u,pm_load(&m,p+(alternate ? 22u : 20u),2),2);pm_store(&m,p+17u,!pm_load(&m,p+17u,1),1); }
                else pm_store(&m,p+18u,pm_load(&m,p+18u,2)-(uint32_t)dt,2);
            }
        }
    }
    return m.error;
}
int frame_lamps_render(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t limit,const frame_actor_services *g)
{
    physics_memory m={read,write,ctx,0};uint32_t o,callback,i,j,p,mask,visible;int status;
    if (!read || !write || !limit || !g) return -1;
    o=object_of(&m,entity);callback=pm_load(&m,o+0xa6u,4);
    if (callback!=0x4138c0u && callback!=0x413930u) return m.error ? m.error : -5;
    mask=callback==0x4138c0u ? 255u : pm_load(&m,0x54b4a8u,4) ? 2u : 1u;
    for (i=0;i<pm_load(&m,o+0x78u,4);i++) {
        if (i>=limit) return -3;
        p=0x53e578u+i*24u;visible=pm_load(&m,p+15u,1)&mask;if (!visible) continue;
        for (j=0;j<3u;j++) pm_store(&m,o+0xa0u+j,pm_load(&m,p+12u+j,1),1);
        for (j=0;j<3u;j++) pm_store(&m,o+0x80u+j*4u,pm_load(&m,p+j*4u,4),4);
        if (m.error) return m.error;
        if (!g->sprite) return -5;
        status=g->sprite(g->sprite_ctx,o+0x7cu);if (status) return status;
    }
    return m.error;
}
#undef LAMP_TRY
