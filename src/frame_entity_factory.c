#include "frame_entity_factory.h"
#include "frame_ability_boost.h"
#include "frame_deformable.h"
#include "frame_objects.h"
#include "physics_memory.h"

int frame_level_allocate(frame_data_read read,frame_data_write write,void *ctx,uint32_t bytes,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t pointer,next;
    if (!read || !write || !out) return -1;
    pointer=pm_load(&m,0x55ae50u,4);if (m.error) return m.error;
    if (!pointer) return -4;
    next=(pointer+bytes+3u)&0xfffffffcu;pm_store(&m,0x55ae50u,next,4);
    if (m.error) return m.error;
    if (next>0x6acb40u) return -4;
    *out=pointer;return 0;
}
int frame_entity_allocate_object(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                                    const frame_entity_factory_services *s,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t record,klass,bytes,object,i,work=0;int status;
    if (!read || !write || !out || !s || !s->work_limit) return -1;
    record=pm_load(&m,pm_load(&m,0x55c694u,4)+entity*8u,4);klass=pm_load(&m,record,4);
    bytes=pm_load(&m,klass+0x14u,4);if (m.error) return m.error;
    if (pm_load(&m,0x55c688u,4)) status=frame_level_allocate(read,write,ctx,bytes,&object);
    else status=frame_pool_allocate(read,write,ctx,bytes,s->allocate,s->release,s->system_ctx,s->work_limit,&object);
    if (status) return status;
    pm_store(&m,record+4u,object,4);
    if (object) {
        bytes=pm_load(&m,klass+0x14u,4);
        for (i=0;i<(bytes>>2);i++) {
            if (work++>=s->work_limit) return -3;
            pm_store(&m,object+i*4u,0,4);if (m.error) return m.error;
        }
        for (i=bytes&0xfffffffcu;i<bytes;i++) pm_store(&m,object+i,0,1);
    }
    if (pm_load(&m,0x55c688u,4)) {
        object=pm_load(&m,record+4u,4);pm_store(&m,object+0x10u,pm_load(&m,object+0x10u,4)|8u,4);
    }
    if (m.error) return m.error;
    *out=1;return 0;
}
int frame_entity_release_object(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                                   const frame_entity_factory_services *s,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t record,klass,object,bytes,result=0;int status;
    if (!read || !write || !out || !s) return -1;
    record=pm_load(&m,pm_load(&m,0x55c694u,4)+entity*8u,4);
    klass=pm_load(&m,record,4);object=pm_load(&m,record+4u,4);
    if (object && !(pm_load(&m,object+0x10u,1)&8u)) {
        bytes=pm_load(&m,klass+0x14u,4);if (m.error) return m.error;
        status=frame_pool_release(read,write,ctx,object,bytes,s->release,s->system_ctx);if (status) return status;
        result=1;
    }
    if (m.error) return m.error;
    *out=result;return 0;
}
int frame_entity_native_base(void *ctx,uint32_t target,uint32_t entity,uint32_t has_payload,
                               uint32_t payload,uint32_t *out)
{
    frame_entity_native_context *c=(frame_entity_native_context *)ctx;int status;
    (void)has_payload;(void)payload;
    if (!c || !c->read || !c->write || !out) return -1;
    switch (target) {
    case 0x407000u:*out=0;return 0;
    case 0x437be0u:*out=1;return 0;
    case 0x504010u:*out=entity;return 0;
    case 0x437bf0u:return frame_ability_boost_apply(c->read,c->write,c->data_ctx,entity,out);
    case 0x4079a0u:return frame_deformable_initialize(c->read,c->write,c->data_ctx,entity,out);
    case 0x407760u:return frame_deformable_set_flag(c->read,c->write,c->data_ctx,entity,payload,out);
    case 0x408170u:return frame_deformable_hit(c->read,c->write,c->data_ctx,entity,payload,out);
    case 0x407f70u:return frame_deformable_tick(c->read,c->write,c->data_ctx,entity,&c->services,out);
    case 0x407790u:return frame_deformable_enter(c->read,c->write,c->data_ctx,entity,&c->services,out);
    case 0x4077e0u:return frame_deformable_leave(c->read,c->write,c->data_ctx,entity,&c->services,out);
    case 0x407f00u:return frame_deformable_destroy(c->read,c->write,c->data_ctx,entity,&c->services,out);
    case 0x4079f0u:return frame_deformable_construct(c->read,c->write,c->data_ctx,entity,payload,&c->services,out);
    case 0x41e890u:return frame_object_destroy(c->read,c->write,c->data_ctx,entity,&c->services,out);
    case 0x41f6e0u:return frame_object_initialize(c->read,c->write,c->data_ctx,entity,&c->services,out);
    case 0x504000u:
        status=frame_entity_queue(c->read,c->write,c->data_ctx,entity,1,c->services.allocate,c->services.release,c->services.system_ctx,c->services.work_limit);
        if (!status) *out=entity;
        return status;
    case 0x437bc0u: {
        physics_memory m={c->read,c->write,c->data_ctx,0};uint32_t klass=pm_load(&m,0x5466e4u,4),unused;
        if (m.error) return m.error;
        status=frame_class_dispatch(c->read,c->data_ctx,klass,entity,c->services.method,c->services.method_ctx,&unused);
        if (!status) *out=entity;
        return status;
    }
    case 0x504e50u:return frame_entity_allocate_object(c->read,c->write,c->data_ctx,entity,&c->services,out);
    case 0x504eb0u:return frame_entity_release_object(c->read,c->write,c->data_ctx,entity,&c->services,out);
    case 0x503cf0u:return frame_entity_enable_tick(c->read,c->write,c->data_ctx,entity,c->services.allocate,c->services.release,c->services.system_ctx,c->services.work_limit,out);
    case 0x503d30u:return frame_entity_disable_tick(c->read,c->write,c->data_ctx,entity,c->services.release,c->services.system_ctx,c->services.work_limit,out);
    case 0x503d80u:return frame_entity_activate(c->read,c->write,c->data_ctx,entity,c->services.method,c->services.method_ctx,out);
    case 0x503ea0u:
        status=frame_entity_reset_transform(c->read,c->write,c->data_ctx,entity);if (!status) *out=entity;return status;
    case 0x504c40u:
        status=frame_entity_release_id(c->read,c->write,c->data_ctx,entity);if (!status) *out=1;return status;
    case 0x504c50u: {
        physics_memory m={c->read,c->write,c->data_ctx,0};
        pm_store(&m,pm_load(&m,0x55c694u,4)+entity*8u,0,4);
        if (m.error) return m.error;
        *out=1;return 0;
    }
    default:return -5;
    }
}

int frame_entity_table_initialize(frame_data_read read,frame_data_write write,void *ctx,uint32_t capacity,const frame_entity_factory_services *s)
{
    physics_memory m={read,write,ctx,0};uint32_t table,i=1,used=0;int status;
    if (!read || !write || !s || !s->work_limit) return -1;
    if (pm_load(&m,0x55c690u,4)!=capacity) {
        table=pm_load(&m,0x55c694u,4);if (m.error) return m.error;
        if (table) { if (!s->release) return -5;status=s->release(s->system_ctx,table);if (status) return status; }
        pm_store(&m,0x55c694u,0,4);pm_store(&m,0x55c690u,capacity,4);
    }
    if (!pm_load(&m,0x55c694u,4)) {
        if (m.error) return m.error;
        if (!s->zero_allocate) return -5;
        status=s->zero_allocate(s->system_ctx,8,pm_load(&m,0x55c690u,4),&table);if (status) return status;
        pm_store(&m,0x55c694u,table,4);
    }
    pm_store(&m,0x55c68cu,0,4);
    table=pm_load(&m,0x55c694u,4);if (m.error) return m.error;
    if (!table) return -4;
    while (pm_signed(i)<pm_sub(pm_i32(&m,0x55c690u),1)) {
        if (used++>=s->work_limit) return -3;
        pm_store(&m,pm_load(&m,0x55c694u,4)+4u+i*8u,i+1u,4);i++;if (m.error) return m.error;
    }
    pm_store(&m,pm_load(&m,0x55c694u,4)+pm_load(&m,0x55c690u,4)*8u-4u,0,4);
    pm_store(&m,0x536cbcu,1,4);pm_store(&m,0x536cc0u,1,4);return m.error;
}
int frame_entity_table_grow(frame_data_read read,frame_data_write write,void *ctx,const frame_entity_factory_services *s)
{
    physics_memory m={read,write,ctx,0};uint32_t old,table,capacity,words,i,work=0;int status;
    if (!read || !write || !s || !s->work_limit) return -1;
    old=pm_load(&m,0x55c694u,4);capacity=pm_load(&m,0x55c690u,4);
    if (m.error) return m.error;
    if (!s->zero_allocate) return -5;
    status=s->zero_allocate(s->system_ctx,8,capacity+128u,&table);if (status) return status;
    words=(pm_load(&m,0x55c690u,4)<<3)>>2;
    pm_store(&m,0x55c694u,table,4);
    if (m.error) return m.error;
    if (!table) return -4; /* Original writes the null table then faults. */
    for (i=0;i<words;i++) {
        if (work++>=s->work_limit) return -3;
        pm_store(&m,table+i*4u,pm_load(&m,old+i*4u,4),4);
        if (m.error) return m.error;
    }
    i=pm_load(&m,0x55c690u,4);
    while (pm_signed(i)<pm_signed(pm_load(&m,0x55c690u,4)+127u)) {
        if (work++>=s->work_limit) return -3;
        pm_store(&m,pm_load(&m,0x55c694u,4)+i*8u+4u,i+1u,4);i++;
        if (m.error) return m.error;
    }
    capacity=pm_load(&m,0x55c690u,4);
    pm_store(&m,pm_load(&m,0x55c694u,4)+capacity*8u+0x3fcu,0,4);
    if (!pm_load(&m,0x536cbcu,4)) {
        pm_store(&m,0x536cbcu,pm_load(&m,0x55c690u,4),4);
        pm_store(&m,0x536cc0u,pm_load(&m,0x55c690u,4),4);
    }
    pm_store(&m,0x55c690u,pm_load(&m,0x55c690u,4)+128u,4);
    if (m.error) return m.error;
    return s->release ? s->release(s->system_ctx,old) : -5;
}
int frame_entity_unlink_free_id(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t work_limit,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t head,current,next,work=0;
    if (!read || !write || !out || !work_limit) return -1;
    head=pm_load(&m,0x536cbcu,4);
    if (head==entity) {
        pm_store(&m,0x536cbcu,pm_load(&m,pm_load(&m,0x55c694u,4)+head*8u+4u,4),4);
        if (m.error) return m.error;
        *out=entity;return 0;
    }
    current=head;next=pm_load(&m,pm_load(&m,0x55c694u,4)+head*8u+4u,4);
    while (next && next!=entity) {
        if (m.error) return m.error;
        if (work++>=work_limit) return -3;
        current=next;next=pm_load(&m,pm_load(&m,0x55c694u,4)+next*8u+4u,4);
    }
    if (next || !entity)
        pm_store(&m,pm_load(&m,0x55c694u,4)+current*8u+4u,
                 pm_load(&m,pm_load(&m,0x55c694u,4)+next*8u+4u,4),4);
    if (m.error) return m.error;
    *out=next ? entity : 0;return 0;
}
int frame_entity_bind_record(frame_data_read read,frame_data_write write,void *ctx,uint32_t record,uint32_t requested,
                                const frame_entity_factory_services *s,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t entity=requested,work=0,unused;int status;
    if (!read || !write || !out || !s || !s->work_limit) return -1;
    if (!entity) {
        entity=pm_load(&m,0x536cbcu,4);
        if (!entity) { status=frame_entity_table_grow(read,write,ctx,s);if (status) return status;entity=pm_load(&m,0x536cbcu,4); }
        pm_store(&m,0x536cbcu,pm_load(&m,pm_load(&m,0x55c694u,4)+entity*8u+4u,4),4);
    } else {
        while (pm_signed(entity)>pm_signed(pm_load(&m,0x55c690u,4)-1u)) {
            if (m.error) return m.error;
            if (work++>=s->work_limit) return -3;
            status=frame_entity_table_grow(read,write,ctx,s);if (status) return status;
        }
        if (!pm_load(&m,pm_load(&m,0x55c694u,4)+entity*8u,4)) {
            if (m.error) return m.error;
            status=frame_entity_unlink_free_id(read,write,ctx,entity,s->work_limit,&unused);if (status) return status;
        }
    }
    pm_store(&m,pm_load(&m,0x55c694u,4)+entity*8u,record,4);
    if (pm_i32(&m,0x536cc0u)<pm_signed(entity)) pm_store(&m,0x536cc0u,entity,4);
    if (m.error) return m.error;
    *out=entity;return 0;
}
int frame_entity_new_record(frame_data_read read,frame_data_write write,void *ctx,uint32_t requested,
                               const frame_entity_factory_services *s,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t record,entity=0;int status;
    if (!read || !write || !out || !s) return -1;
    status=frame_pool_allocate(read,write,ctx,8,s->allocate,s->release,s->system_ctx,s->work_limit,&record);
    if (status) return status;
    if (record) {
        pm_store(&m,record,0,4);pm_store(&m,record+4u,0,4);if (m.error) return m.error;
        status=frame_entity_bind_record(read,write,ctx,record,requested,s,&entity);if (status) return status;
        pm_store(&m,0x55c68cu,pm_load(&m,0x55c68cu,4)+1u,4);
    }
    if (m.error) return m.error;
    *out=entity;return 0;
}
int frame_entity_create(frame_data_read read,frame_data_write write,void *ctx,uint32_t klass,
                           const frame_entity_factory_services *s,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t requested=0,entity,record,result,descriptor;int status;
    if (!read || !write || !out || !s) return -1;
    if (pm_load(&m,0x55c680u,4)) {
        requested=pm_load(&m,0x536cb0u,4);pm_store(&m,0x536cb0u,requested+1u,4);
    }
    if (m.error) return m.error;
    status=frame_entity_new_record(read,write,ctx,requested,s,&entity);if (status) return status;
    if (entity) {
        record=pm_load(&m,pm_load(&m,0x55c694u,4)+entity*8u,4);
        if (record) {
            pm_store(&m,record,klass,4);descriptor=pm_load(&m,0x545ef8u,4);if (m.error) return m.error;
            status=frame_entity_dispatch(read,write,ctx,entity,descriptor,0,0,s->method,s->method_ctx,&result);
            if (status) return status;
            descriptor=pm_load(&m,0x545ebcu,4);if (m.error) return m.error;
            status=frame_entity_dispatch(read,write,ctx,entity,descriptor,0,0,s->method,s->method_ctx,&result);
            if (status) return status;
        }
    }
    if (m.error) return m.error;
    *out=entity;return 0;
}
int frame_entity_fill_reserved(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t klass,
                                  const frame_entity_factory_services *s)
{
    physics_memory m={read,write,ctx,0};uint32_t record,descriptor,unused;int status;
    if (!read || !write || !s) return -1;
    if (!entity || entity>=pm_load(&m,0x55c690u,4)) return m.error ? m.error : -4;
    record=pm_load(&m,pm_load(&m,0x55c694u,4)+entity*8u,4);
    if (m.error) return m.error;
    if (!record || pm_load(&m,record,4) || pm_load(&m,record+4u,4)) return m.error ? m.error : -4;
    pm_store(&m,record,klass,4);descriptor=pm_load(&m,0x545ef8u,4);if (m.error) return m.error;
    status=frame_entity_dispatch(read,write,ctx,entity,descriptor,0,0,s->method,s->method_ctx,&unused);if (status) return status;
    descriptor=pm_load(&m,0x545ebcu,4);if (m.error) return m.error;
    return frame_entity_dispatch(read,write,ctx,entity,descriptor,0,0,s->method,s->method_ctx,&unused);
}
