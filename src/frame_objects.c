#include "frame_objects.h"
#include "frame_shape.h"
#include "frame_spatial.h"
#include "physics_memory.h"

static uint32_t object_of(physics_memory *m,uint32_t entity)
{
    uint32_t record=pm_load(m,pm_load(m,0x55c694u,4)+entity*8u,4);
    return record ? pm_load(m,record+4u,4) : 0;
}
int frame_object_move(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t position,const frame_entity_factory_services *s,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t o,i,packed;int status;
    if (!read || !write || !s || !s->work_limit || !out) return -1;
    o=object_of(&m,entity);for (i=0;i<3u;i++) pm_store(&m,o+i*4u,pm_load(&m,position+i*4u,4),4);
    packed=pm_load(&m,o+0x90u,4);if (m.error) return m.error;
    status=frame_spatial_remove(read,write,ctx,entity,packed,s->release,s->system_ctx,s->work_limit);if (status) return status;
    status=frame_spatial_insert(read,write,ctx,entity,s->allocate,s->release,s->system_ctx,s->work_limit,&packed);if (status) return status;
    pm_store(&m,o+0x90u,packed,4);*out=pm_load(&m,position+4u,4);return m.error;
}
int frame_object_collidable(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t mode,const frame_entity_factory_services *s)
{
    physics_memory m={read,write,ctx,0};uint32_t o,packed;int status;
    if (!read || !write || !s || !s->work_limit) return -1;
    o=object_of(&m,entity);if (m.error) return m.error;
    if (mode==1u) { status=frame_spatial_insert(read,write,ctx,entity,s->allocate,s->release,s->system_ctx,s->work_limit,&packed);if (status) return status;pm_store(&m,o+0x90u,packed,4);return m.error; }
    packed=pm_load(&m,o+0x90u,4);if (m.error) return m.error;
    return frame_spatial_remove(read,write,ctx,entity,packed,s->release,s->system_ctx,s->work_limit);
}
static int release_fields(physics_memory *m,uint32_t pointer_at,uint32_t size_at,const frame_entity_factory_services *s)
{
    uint32_t pointer=pm_load(m,pointer_at,4),bytes=pm_load(m,size_at,4);
    if (m->error) return m->error;
    return frame_pool_release(m->read,m->write,m->ctx,pointer,bytes,s->release,s->system_ctx);
}
static int parent_call(physics_memory *m,uint32_t entity,const frame_entity_factory_services *s)
{
    uint32_t klass=pm_load(m,0x53e4b0u,4),unused;if (m->error) return m->error;
    return frame_class_dispatch(m->read,m->ctx,klass,entity,s->method,s->method_ctx,&unused);
}
int frame_object_initialize(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                               const frame_entity_factory_services *s,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t o;int status;
    if (!read || !write || !out || !s || !s->work_limit) return -1;
    o=object_of(&m,entity);if (m.error) return m.error;
    status=parent_call(&m,entity,s);if (status) return status;
    pm_store(&m,o+0x90u,UINT32_MAX,4);if (m.error) return m.error;
    *out=entity;return 0;
}
int frame_object_enter(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                          const frame_entity_factory_services *s)
{
    physics_memory m={read,write,ctx,0};uint32_t o,head,node,packed;int status;
    if (!read || !write || !s || !s->work_limit) return -1;
    o=object_of(&m,entity);head=pm_load(&m,0x54b48cu,4);if (m.error) return m.error;
    status=frame_list_append(read,write,ctx,entity,head,s->allocate,s->release,s->system_ctx,s->work_limit,&node);if (status) return status;
    status=frame_spatial_insert(read,write,ctx,entity,s->allocate,s->release,s->system_ctx,s->work_limit,&packed);if (status) return status;
    pm_store(&m,o+0x90u,packed,4);return m.error;
}
int frame_object_leave(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                          const frame_entity_factory_services *s)
{
    physics_memory m={read,write,ctx,0};uint32_t o,head,packed;int status;
    if (!read || !write || !s || !s->work_limit) return -1;
    o=object_of(&m,entity);head=pm_load(&m,0x54b48cu,4);if (m.error) return m.error;
    status=frame_list_remove_value(read,write,ctx,entity,head,s->release,s->system_ctx,s->work_limit);if (status) return status;
    packed=pm_load(&m,o+0x90u,4);if (m.error) return m.error;
    return frame_spatial_remove(read,write,ctx,entity,packed,s->release,s->system_ctx,s->work_limit);
}
int frame_object_deactivate(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                               const frame_entity_factory_services *s)
{
    physics_memory m={read,write,ctx,0};uint32_t o,packed,head;int status;
    if (!read || !write || !s || !s->work_limit) return -1;
    o=object_of(&m,entity);if (m.error) return m.error;
    status=parent_call(&m,entity,s);if (status) return status;
    packed=pm_load(&m,o+0x90u,4);if (m.error) return m.error;
    status=frame_spatial_remove(read,write,ctx,entity,packed,s->release,s->system_ctx,s->work_limit);if (status) return status;
    head=pm_load(&m,0x54b48cu,4);if (m.error) return m.error;
    status=frame_list_remove_value(read,write,ctx,entity,head,s->release,s->system_ctx,s->work_limit);if (status) return status;
    head=pm_load(&m,0x54b488u,4);if (m.error) return m.error;
    return frame_list_remove_value(read,write,ctx,entity,head,s->release,s->system_ctx,s->work_limit);
}
int frame_object_set_mode(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t mode,
                             const frame_entity_factory_services *s)
{
    physics_memory m={read,write,ctx,0};uint32_t head,node;int status;
    if (!read || !write || !s || !s->work_limit) return -1;
    head=pm_load(&m,mode==1u ? 0x54b48cu : 0x54b488u,4);if (m.error) return m.error;
    status=frame_list_remove_value(read,write,ctx,entity,head,s->release,s->system_ctx,s->work_limit);if (status) return status;
    head=pm_load(&m,mode==1u ? 0x54b488u : 0x54b48cu,4);if (m.error) return m.error;
    return frame_list_append(read,write,ctx,entity,head,s->allocate,s->release,s->system_ctx,s->work_limit,&node);
}
int frame_object_auxiliary_create(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                                     const frame_entity_factory_services *s)
{
    physics_memory m={read,write,ctx,0};uint32_t o,storage;int status;
    if (!read || !write || !s || !s->work_limit) return -1;
    o=object_of(&m,entity);if (m.error) return m.error;
    status=frame_pool_allocate_zero(read,write,ctx,92,s->allocate,s->release,s->system_ctx,s->work_limit,&storage);if (status) return status;
    pm_store(&m,o+0x88u,storage,4);return m.error;
}
int frame_object_auxiliary_clear(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                                    const frame_entity_factory_services *s)
{
    physics_memory m={read,write,ctx,0};uint32_t o,a,shape,bytes;int status;
    if (!read || !write || !s || !s->work_limit) return -1;
    o=object_of(&m,entity);a=pm_load(&m,o+0x88u,4);
    if (!a) return m.error;
    if (pm_load(&m,a+0x21u,1)) {
        if (m.error) return m.error;
        status=release_fields(&m,a+0x48u,a+0x4cu,s);if (status) return status;
        a=pm_load(&m,o+0x88u,4);pm_store(&m,a+0x4cu,0,4);a=pm_load(&m,o+0x88u,4);
        if (m.error) return m.error;
        status=release_fields(&m,a+0x50u,a+0x54u,s);if (status) return status;
        a=pm_load(&m,o+0x88u,4);pm_store(&m,a+0x54u,0,4);a=pm_load(&m,o+0x88u,4);pm_store(&m,a+0x21u,0,1);
    }
    a=pm_load(&m,o+0x88u,4);
    if (pm_load(&m,a+0x44u,4)) {
        if (m.error) return m.error;
        shape=pm_load(&m,a,4);bytes=pm_load(&m,a+0x44u,4);if (m.error) return m.error;
        status=frame_shape_release(read,write,ctx,shape,bytes,s);if (status) return status;
        a=pm_load(&m,o+0x88u,4);if (m.error) return m.error;
        status=release_fields(&m,a+0x14u,a+0x1cu,s);if (status) return status;
        a=pm_load(&m,o+0x88u,4);pm_store(&m,a+0x44u,0,4);a=pm_load(&m,o+0x88u,4);pm_store(&m,a+0x1cu,0,4);
    }
    return m.error;
}
int frame_object_auxiliary_backup_allocate(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *s)
{
    physics_memory m={read,write,ctx,0};uint32_t o,a,shape,triangles,size,power,buffer,phase,used=0;int status;
    if (!read || !write || !s || !s->work_limit) return -1;
    o=object_of(&m,entity);a=pm_load(&m,o+0x88u,4);shape=pm_load(&m,a,4);triangles=pm_load(&m,shape+2u,2);
    for (phase=0;phase<2u;phase++) {
        a=pm_load(&m,o+0x88u,4);size=phase ? triangles*6u : pm_load(&m,shape,2)*8u;pm_store(&m,a+(phase ? 84u : 76u),size,4);power=2;
        while (power<size) { if (used++>=s->work_limit) return -3;power*=2u; }
        a=pm_load(&m,o+0x88u,4);pm_store(&m,a+(phase ? 84u : 76u),power,4);if (m.error) return m.error;
        status=frame_pool_allocate(read,write,ctx,power,s->allocate,s->release,s->system_ctx,s->work_limit,&buffer);if (status) return status;
        a=pm_load(&m,o+0x88u,4);pm_store(&m,a+(phase ? 80u : 72u),buffer,4);
    }
    return m.error;
}
int frame_object_auxiliary_release(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                                      const frame_entity_factory_services *s)
{
    physics_memory m={read,write,ctx,0};uint32_t o,a;int status;
    if (!read || !write || !s || !s->work_limit) return -1;
    o=object_of(&m,entity);a=pm_load(&m,o+0x88u,4);if (m.error) return m.error;
    if (a) {
        status=frame_pool_release(read,write,ctx,a,92,s->release,s->system_ctx);if (status) return status;
        pm_store(&m,o+0x88u,0,4);
    }
    return m.error;
}
int frame_object_destroy(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                            const frame_entity_factory_services *s,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t o,a,value,klass,unused;int status;
    if (!read || !write || !out || !s || !s->work_limit) return -1;
    o=object_of(&m,entity);
    if (pm_load(&m,o+0x88u,4)) {
        if (m.error) return m.error;
        status=frame_object_auxiliary_clear(read,write,ctx,entity,s);if (status) return status;
        status=frame_object_auxiliary_release(read,write,ctx,entity,s);if (status) return status;
    }
    a=pm_load(&m,o+0x8cu,4);
    if (a) {
        value=pm_load(&m,a+12u,4);
        if (value) {
            if (m.error) return m.error;
            status=release_fields(&m,a+12u,a+16u,s);if (status) return status;
            a=pm_load(&m,o+0x8cu,4);pm_store(&m,a+16u,0,4);a=pm_load(&m,o+0x8cu,4);pm_store(&m,a+12u,0,4);
        }
        a=pm_load(&m,o+0x8cu,4);
        if (pm_load(&m,a+24u,1)&128u) {
            if (m.error) return m.error;
            value=pm_load(&m,a+48u,4);if (m.error) return m.error;
            status=frame_pool_release(read,write,ctx,value,42,s->release,s->system_ctx);if (status) return status;
        }
        a=pm_load(&m,o+0x8cu,4);value=pm_load(&m,a+40u,4);
        if (value) {
            if (m.error) return m.error;
            status=frame_pool_release(read,write,ctx,value,58,s->release,s->system_ctx);if (status) return status;
            a=pm_load(&m,o+0x8cu,4);pm_store(&m,a+40u,0,4);
        }
        a=pm_load(&m,o+0x8cu,4);if (m.error) return m.error;
        status=frame_pool_release(read,write,ctx,a,52,s->release,s->system_ctx);if (status) return status;
        pm_store(&m,o+0x8cu,0,4);
    }
    value=pm_load(&m,o+0x90u,4);if (m.error) return m.error;
    status=frame_spatial_remove(read,write,ctx,entity,value,s->release,s->system_ctx,s->work_limit);if (status) return status;
    klass=pm_load(&m,0x53e4b0u,4);if (m.error) return m.error;
    status=frame_class_dispatch(read,ctx,klass,entity,s->method,s->method_ctx,&unused);if (status) return status;
    *out=entity;return 0;
}
