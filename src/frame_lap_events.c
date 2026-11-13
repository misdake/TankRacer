#include "frame_lap_events.h"
#include "physics_memory.h"
#include "frame_spatial.h"

static uint32_t near_camera(physics_memory *m,uint32_t object,int32_t camera,int32_t threshold)
{
    uint32_t index=(uint32_t)camera*12u;
    int32_t x=pm_sub(pm_sar(pm_i32(m,object),3),pm_i32(m,0x74aee0u+index));
    if (x<0) x=pm_sub(pm_i32(m,0x74aee0u+index),pm_sar(pm_i32(m,object),3));
    if (x<threshold) {
        int32_t z=pm_sub(pm_sar(pm_i32(m,object+8u),3),pm_i32(m,0x74aee8u+index));
        if (z<0) z=pm_sub(pm_i32(m,0x74aee8u+index),pm_sar(pm_i32(m,object+8u),3));
        if (z<threshold) return 1;
    }
    return 0;
}
int frame_entity_near_camera(frame_data_read read,void *ctx,uint32_t object,int32_t camera,
                               int32_t threshold,uint32_t *out)
{
    physics_memory m={read,0,ctx,0};uint32_t result;
    if (!read || !out) return -1;
    camera=pm_signed((uint32_t)camera&255u);if (camera>127) camera-=256;
    result=near_camera(&m,object,camera,threshold);
    if (m.error) return m.error;
    *out=result;return 0;
}
static int lap_events(frame_data_read read,frame_data_write write,void *ctx,uint32_t player,
                              frame_native_method method,void *method_ctx,
                              frame_pool_system_alloc allocate,frame_pool_system_free release,
                              void *system_ctx,uint32_t work_limit,int special)
{
    physics_memory mem={read,write,ctx,0},*m=&mem;
    uint32_t head,node,work=0;
    if (!read || !write || !work_limit) return -1;
    head=pm_load(m,0x53e4b4u,4);node=pm_load(m,head,4);
    while (node!=head) {
        uint32_t id,record,object;
        int action=-1,status;
        if (m->error) return m->error;
        if (work++>=work_limit) return -3;
        id=pm_load(m,node+8u,4);record=pm_load(m,pm_load(m,0x55c694u,4)+id*8u,4);
        object=record ? pm_load(m,record+4u,4) : 0;
        if (object && special && pm_load(m,record,4)==pm_load(m,0x545094u,4)) {
            if (!near_camera(m,object,0,9600) && !near_camera(m,object,1,9600)) {
                if (m->error) return m->error;
                status=frame_object_respawn(read,write,ctx,id,release,system_ctx,work_limit);
                if (status) return status;
            }
            node=pm_load(m,node,4);head=pm_load(m,0x53e4b4u,4);
            continue;
        }
        if (object) {
            int32_t lap=pm_i16(m,player+0xddau);
            if (pm_i16(m,object+0x78u)==lap) action=0;
            else if (pm_i16(m,object+0x7au)==lap) action=1;
        }
        if (action>=0) {
            uint32_t near=0,result;
            if (pm_load(m,0x54b480u,4)) {
                near=near_camera(m,object,0,19200);
                if (!near) near=near_camera(m,object,1,19200);
            }
            if (!near) near=near_camera(m,object,0,19200);
            if (m->error) return m->error;
            if (near) {
                uint32_t queue=pm_load(m,action ? 0x53e4bcu : 0x53e4b8u,4);
                if (m->error) return m->error;
                status=frame_list_append(read,write,ctx,id,queue,allocate,release,system_ctx,work_limit,&result);
            } else {
                uint32_t descriptor=pm_load(m,action ? 0x545f48u : 0x545f44u,4);
                if (m->error) return m->error;
                status=frame_entity_dispatch(read,write,ctx,id,descriptor,0,0,method,method_ctx,&result);
            }
            if (status) return status;
        }
        node=pm_load(m,node,4);head=pm_load(m,0x53e4b4u,4);
    }
    return m->error;
}
int frame_lap_object_events(frame_data_read read,frame_data_write write,void *ctx,uint32_t player,
                              frame_native_method method,void *method_ctx,
                              frame_pool_system_alloc allocate,frame_pool_system_free release,
                              void *system_ctx,uint32_t work_limit)
{
    return lap_events(read,write,ctx,player,method,method_ctx,allocate,release,system_ctx,work_limit,0);
}
int frame_lap_object_events_special(frame_data_read read,frame_data_write write,void *ctx,uint32_t player,
                                      frame_native_method method,void *method_ctx,
                                      frame_pool_system_alloc allocate,frame_pool_system_free release,
                                      void *system_ctx,uint32_t work_limit)
{
    return lap_events(read,write,ctx,player,method,method_ctx,allocate,release,system_ctx,work_limit,1);
}
int frame_lap_flush_deferred(frame_data_read read,frame_data_write write,void *ctx,
                               frame_native_method method,void *method_ctx,
                               frame_pool_system_free release,void *release_ctx,uint32_t work_limit)
{
    physics_memory mem={read,write,ctx,0},*m=&mem;
    uint32_t work=0;
    unsigned phase;
    if (!read || !write || !work_limit) return -1;
    for (phase=0;phase<2;phase++) {
        uint32_t global=phase ? 0x53e4bcu : 0x53e4b8u;
        uint32_t head=pm_load(m,global,4),node=pm_load(m,head,4);
        while (node!=head) {
            uint32_t record,object,near=0,id;
            int status;
            if (m->error) return m->error;
            if (work++>=work_limit) return -3;
            id=pm_load(m,node+8u,4);record=pm_load(m,pm_load(m,0x55c694u,4)+id*8u,4);
            object=record ? pm_load(m,record+4u,4) : 0;
            if (object) {
                near=near_camera(m,object,0,19200);
                if (!near && pm_load(m,0x53e8bcu,4)) near=near_camera(m,object,1,19200);
                if (!near) {
                    uint32_t descriptor=pm_load(m,phase ? 0x545f48u : 0x545f44u,4),result,old=node;
                    if (m->error) return m->error;
                    status=frame_entity_dispatch(read,write,ctx,id,descriptor,0,0,method,method_ctx,&result);
                    if (status) return status;
                    node=pm_load(m,old,4);
                    if (m->error) return m->error;
                    status=frame_list_remove(read,write,ctx,old,release,release_ctx);
                    if (status) return status;
                }
            }
            /* Deliberately advance again after a removed node. If node is now
             * the sentinel this reads the updated first node, just like x86. */
            node=pm_load(m,node,4);head=pm_load(m,global,4);
        }
    }
    return m->error;
}
