#include "frame_spatial.h"
#include "physics_memory.h"
int frame_spatial_initialize(frame_data_read read,frame_data_write write,void *ctx,
                              frame_pool_system_alloc allocate,frame_pool_system_free release,
                              void *system_ctx,uint32_t limit)
{
    physics_memory m={read,write,ctx,0};uint32_t root,i,head;int status;
    if (!read || !write || !limit) return -1;
    root=pm_load(&m,0x55ae60u,4);
    pm_store(&m,0x544f70u,pm_load(&m,root+0x108u,4)<<3,4);
    pm_store(&m,0x544f74u,pm_load(&m,root+0x10cu,4)<<3,4);
    pm_store(&m,0x544f78u,pm_load(&m,root+0x110u,2)+3u,4);
    pm_store(&m,0x544f7cu,pm_load(&m,root+0x112u,2)+3u,4);
    for (i=0;i<64u;i++) {
        if (m.error) return m.error;
        if (i>=limit) return -3;
        status=frame_list_create(read,write,ctx,allocate,release,system_ctx,limit,&head);if (status) return status;
        pm_store(&m,0x544e70u+i*4u,head,4);
    }
    return m.error;
}

static uint32_t entity_object(physics_memory *m,uint32_t id)
{
    uint32_t record=pm_load(m,pm_load(m,0x55c694u,4)+id*8u,4);
    return record ? pm_load(m,record+4u,4) : 0;
}
int frame_spatial_insert(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                           frame_pool_system_alloc allocate,frame_pool_system_free release,
                           void *system_ctx,uint32_t work_limit,uint32_t *packed)
{
    physics_memory mem={read,write,ctx,0},*m=&mem;
    uint32_t object,shape,result=UINT32_MAX;
    int32_t gx,gz,radius=5632,row,col;
    if (!read || !write || !packed || !work_limit) return -1;
    object=entity_object(m,entity);
    if (object) {
        gx=pm_sar(pm_sub(pm_i32(m,object),pm_i32(m,0x544f70u)),pm_load(m,0x544f78u,4));
        gz=pm_sar(pm_sub(pm_i32(m,object+8u),pm_i32(m,0x544f74u)),pm_load(m,0x544f7cu,4));
        if (gx<0) gx=0;else if (gx>7) gx=7;
        if (gz<0) gz=0;else if (gz>7) gz=7;
        shape=pm_load(m,object+0x48u,4);
        if (shape) radius=pm_mul(pm_add((int32_t)pm_load(m,shape+12u,2),352),16);
        for (row=gz-1;row<=gz+1;row++) if (row>=0 && row<8) {
            uint32_t shift=pm_load(m,0x544f7cu,4)&31u;
            int32_t lower=pm_add(pm_signed((uint32_t)row<<shift),pm_i32(m,0x544f74u));
            int32_t z=pm_i32(m,object+8u);
            if (pm_sub(z,radius)<pm_add(lower,pm_signed(1u<<shift)) && lower<pm_add(radius,z)) {
                for (col=gx-1;col<=gx+1;col++) if (col>=0 && col<8) {
                    uint32_t head,node;
                    int status;
                    int32_t x=pm_i32(m,object);
                    shift=pm_load(m,0x544f78u,4)&31u;
                    lower=pm_add(pm_signed((uint32_t)col<<shift),pm_i32(m,0x544f70u));
                    if (pm_sub(x,radius)>=pm_add(lower,pm_signed(1u<<shift)) || pm_add(radius,x)<=lower) continue;
                    head=pm_load(m,0x544e70u+((uint32_t)col+(uint32_t)row*8u)*4u,4);
                    if (m->error) return m->error;
                    status=frame_list_prepend(read,write,ctx,entity,head,allocate,release,system_ctx,work_limit,&node);
                    if (status) return status;
                }
            }
        }
        result=((uint32_t)gx<<16)|(uint32_t)gz;
    }
    if (m->error) return m->error;
    *packed=result;return 0;
}
int frame_spatial_remove(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t packed,
                           frame_pool_system_free release,void *release_ctx,uint32_t work_limit)
{
    physics_memory mem={read,write,ctx,0},*m=&mem;
    int32_t x=pm_sar(pm_signed(packed),16),z=(int32_t)(packed&65535u),row,col;
    if (!read || !write || !work_limit) return -1;
    if (packed!=UINT32_MAX) for (row=z-1;row<=z+1;row++) if (row>=0 && row<8)
        for (col=x-1;col<=x+1;col++) if (col>=0 && col<8) {
            uint32_t head=pm_load(m,0x544e70u+((uint32_t)col+(uint32_t)row*8u)*4u,4);
            int status;
            if (m->error) return m->error;
            status=frame_list_remove_value(read,write,ctx,entity,head,release,release_ctx,work_limit);
            if (status) return status;
        }
    return m->error;
}
int frame_object_variant(frame_data_read read,frame_data_write write,void *ctx,uint32_t object,uint32_t work_limit)
{
    physics_memory mem={read,write,ctx,0},*m=&mem;
    uint32_t seed,threshold,roll,work=0;
    int32_t index;
    if (!read || !write || !work_limit) return -1;
    seed=pm_load(m,0x55b6acu,4)*1664525u+1013904223u;pm_store(m,0x55b6acu,seed,4);
    if (!pm_load(m,0x58e78cu,4)) {
        roll=(seed>>4)&31u;index=pm_i32(m,0x543d00u);threshold=0x530ce2u+(uint32_t)index*7u;
        while (roll<pm_load(m,threshold,1)) {
            if (m->error) return m->error;
            if (work++>=work_limit) return -3;
            index=pm_sub(index,1);threshold--;
        }
        pm_store(m,object+0x173u,(uint32_t)pm_add(index,1),1);
    }
    return m->error;
}
int frame_object_respawn(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                           frame_pool_system_free release,void *release_ctx,uint32_t work_limit)
{
    physics_memory mem={read,write,ctx,0},*m=&mem;
    uint32_t object,head,packed;unsigned i;int status;
    if (!read || !write || !work_limit) return -1;
    object=entity_object(m,entity);
    pm_store(m,object+0x153u,0,4);pm_store(m,object+0x157u,0,4);pm_store(m,object+0x13fu,0,4);
    pm_store(m,object,pm_load(m,object+0xbau,4),4);pm_store(m,object+0x13bu,0,4);
    pm_store(m,object+0x137u,0,4);pm_store(m,object+4u,pm_load(m,object+0xbeu,4),4);
    pm_store(m,object+8u,pm_load(m,object+0xc2u,4),4);
    for (i=0;i<9;i++) pm_store(m,object+0x18u+i*4u,pm_load(m,object+0xc6u+i*4u,4),4);
    pm_store(m,object+0xaau,pm_load(m,object+0xaau,4)&0xfffffff2u,4);
    head=pm_load(m,0x54b488u,4);
    if (m->error) return m->error;
    status=frame_list_remove_value(read,write,ctx,entity,head,release,release_ctx,work_limit);if (status) return status;
    packed=pm_load(m,object+0x90u,4);
    if (m->error) return m->error;
    status=frame_spatial_remove(read,write,ctx,entity,packed,release,release_ctx,work_limit);if (status) return status;
    return frame_object_variant(read,write,ctx,object,work_limit);
}
int frame_object_restore(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                           frame_pool_system_alloc allocate,frame_pool_system_free release,
                           void *system_ctx,uint32_t work_limit)
{
    physics_memory mem={read,write,ctx,0},*m=&mem;uint32_t object,packed;unsigned i;int status;
    if (!read || !write || !work_limit) return -1;
    object=entity_object(m,entity);
    if ((pm_load(m,object+0xeau,1)&1u) && !pm_load(m,object+0x80u,4)) {
        for (i=0;i<9;i++) pm_store(m,object+0x18u+i*4u,pm_load(m,object+0xaau+i*4u,4),4);
        pm_store(m,object+0xeau,0x20,4);
        if (m->error) return m->error;
        status=frame_spatial_insert(read,write,ctx,entity,allocate,release,system_ctx,work_limit,&packed);
        if (status) return status;
        pm_store(m,object+0x90u,packed,4);
    }
    return m->error;
}
