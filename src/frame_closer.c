#include "frame_closer.h"
#include "frame_camera.h"
#include "physics_memory.h"
static uint32_t object_of(physics_memory *m,uint32_t entity)
{ uint32_t r=pm_load(m,pm_load(m,0x55c694u,4)+entity*8u,4);return r ? pm_load(m,r+4u,4) : 0; }
#define TRY(x) do { if (m.error) return m.error;status=(x);if (status) return status; } while (0)
int frame_closer_construct(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t parser,const frame_entity_factory_services *e,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t o,parent,unused;int status;
    if (!read || !write || !e || !e->work_limit || !out) return -1;
    o=object_of(&m,entity);parent=pm_load(&m,0x544f80u,4);TRY(frame_class_dispatch_payload(read,ctx,parent,entity,parser,e->method,e->method_ctx,&unused));
    pm_store(&m,o+0xaau,1820,4);pm_store(&m,o+0xaeu,4,4);TRY(frame_entity_cancel_notification(read,write,ctx,entity,e->release,e->system_ctx,e->work_limit));pm_store(&m,o+0xb2u,0x406490u,4);*out=0;return m.error;
}
int frame_closer_hit(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t vehicle,const frame_entity_factory_services *e,frame_effect_sink feedback,void *feedback_ctx)
{
    physics_memory m={read,write,ctx,0};uint32_t o,tank,d,unused,shape;int status;
    if (!read || !write || !e || !e->work_limit) return -1;
    o=object_of(&m,entity);tank=object_of(&m,vehicle);
    if (pm_load(&m,o+0xaeu,1)&1u) TRY(frame_entity_activate(read,write,ctx,entity,e->method,e->method_ctx,&unused));
    if (pm_load(&m,o+0x88u,4)) { d=pm_load(&m,0x545ee4u,4);TRY(frame_entity_dispatch(read,write,ctx,entity,d,1,vehicle,e->method,e->method_ctx,&unused));pm_store(&m,o+0xb2u,0x406480u,4); }
    if (pm_load(&m,tank+0xde5u,1)) { shape=pm_load(&m,o+0x48u,4);TRY(frame_feedback_impact(read,write,ctx,pm_load(&m,shape+12u,2),feedback,feedback_ctx)); }
    return m.error;
}
int frame_closer_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *e,const physics_trig *trig)
{
    physics_memory m={read,write,ctx,0};uint32_t o,phase,player,d,unused,matrix,angles[3],flags,i;int32_t roll,target,dt,accel,speed,delta,values[9];int status,near;
    if (!read || !write || !e || !e->work_limit) return -1;
    o=object_of(&m,entity);phase=pm_load(&m,o+0xb2u,4);
    if (phase==0x406480u) { d=pm_load(&m,0x545ee8u,4);TRY(frame_entity_dispatch(read,write,ctx,entity,d,0,0,e->method,e->method_ctx,&unused));return m.error; }
    if (phase==0x406490u) {
        if (!((entity^pm_load(&m,0x590564u,4))&7u)) {
            player=object_of(&m,pm_load(&m,0x53e8b8u,4));near=entity!=0u;
            if (player) near=pm_abs(pm_sub(pm_i32(&m,player),pm_i32(&m,o)))<40960 && pm_abs(pm_sub(pm_i32(&m,player+8u),pm_i32(&m,o+8u)))<40960;
            if (near) pm_store(&m,o+0xb2u,0x4064e0u,4);
        }
        return m.error;
    }
    if (phase!=0x4064e0u) return -5;
    matrix=pm_load(&m,o+0x14u,4);if (m.error) return m.error;if (!matrix) return -4;
    TRY(frame_camera_matrix_angle_values(read,ctx,matrix,angles,&unused));roll=pm_signed(angles[2]);i=0;
    while (roll>32760) { if (i++>=e->work_limit) return -3;roll=pm_sub(roll,65520); }
    while (roll<=-32760) { if (i++>=e->work_limit) return -3;roll=pm_add(roll,65520); }
    target=pm_neg(roll);flags=pm_load(&m,o+0xaeu,4);dt=pm_i32(&m,0x5904fcu);accel=pm_sar(pm_mul(dt,1820),8);speed=pm_i32(&m,o+0xaau);
    if (flags&4u) pm_write32(&m,o+0xaau,pm_add(speed,accel));
    else if (speed<accel) { flags|=4u;pm_store(&m,o+0xaeu,flags,4);pm_write32(&m,o+0xaau,pm_sub(accel,speed)); }
    else pm_write32(&m,o+0xaau,pm_sub(speed,accel));
    speed=pm_i32(&m,o+0xaau);delta=pm_sar(pm_mul(dt,speed),8);flags=pm_load(&m,o+0xaeu,4);
    if (flags&4u) {
        if (target<0 && pm_abs(target)<delta) {
            delta=target;
            if (pm_signed((uint32_t)speed&~3u)>7280) { pm_store(&m,o+0xaeu,flags&~6u,4);pm_write32(&m,o+0xaau,pm_sar(speed,2)); }
            else { pm_store(&m,o+0xaeu,flags|1u,4);TRY(frame_entity_queue(read,write,ctx,entity,0,e->allocate,e->release,e->system_ctx,e->work_limit)); }
        } else if (target>=0 && target<delta) {
            delta=target;
            if ((uint32_t)speed&~3u) { pm_store(&m,o+0xaeu,(flags&~4u)|2u,4);pm_write32(&m,o+0xaau,pm_sar(speed,2)); }
            else { pm_store(&m,o+0xaeu,flags|1u,4);TRY(frame_entity_queue(read,write,ctx,entity,0,e->allocate,e->release,e->system_ctx,e->work_limit)); }
        } else if (target<0) delta=pm_neg(delta);
    } else if (flags&2u) delta=pm_neg(delta);
    TRY(frame_matrix_rotate(read,write,ctx,matrix,2,(uint32_t)delta,trig));
    for (i=0;i<9u;i++) values[i]=pm_i32(&m,matrix+i*4u);
    TRY(physics_matrix_normalize(values));for (i=0;i<9u;i++) pm_write32(&m,matrix+i*4u,values[i]);return m.error;
}
#undef TRY
