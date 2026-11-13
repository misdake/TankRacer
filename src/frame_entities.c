#include "frame_entities.h"
#include "physics_memory.h"

static uint32_t entity_object(physics_memory *m,uint32_t entity)
{
    uint32_t record=pm_load(m,pm_load(m,0x55c694u,4)+entity*8u,4);
    return record ? pm_load(m,record+4u,4) : 0;
}
int frame_entity_enable_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                               frame_pool_system_alloc allocate,frame_pool_system_free release,
                               void *system_ctx,uint32_t work_limit,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t object,node;int status;
    if (!read || !write || !out || !work_limit) return -1;
    object=entity_object(&m,entity);
    if (pm_load(&m,object+0x10u,1)&16u) { if (m.error) return m.error;*out=0;return 0; }
    node=pm_load(&m,0x55b6a8u,4);if (m.error) return m.error;
    status=frame_list_append(read,write,ctx,entity,node,allocate,release,system_ctx,work_limit,&node);
    if (status) return status;
    pm_store(&m,object+0x10u,pm_load(&m,object+0x10u,4)|16u,4);
    if (m.error) return m.error;
    *out=1;return 0;
}
int frame_entity_disable_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                                frame_pool_system_free release,void *release_ctx,uint32_t work_limit,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t object,cursor,head;int status;
    if (!read || !write || !out || !work_limit) return -1;
    object=entity_object(&m,entity);
    if (!(pm_load(&m,object+0x10u,1)&16u)) { if (m.error) return m.error;*out=0;return 0; }
    cursor=pm_load(&m,0x55b6c0u,4);
    if (cursor && pm_load(&m,cursor+8u,4)==entity) pm_store(&m,0x55b6c0u,pm_load(&m,cursor,4),4);
    head=pm_load(&m,0x55b6a8u,4);if (m.error) return m.error;
    status=frame_list_remove_value(read,write,ctx,entity,head,release,release_ctx,work_limit);if (status) return status;
    pm_store(&m,object+0x10u,pm_load(&m,object+0x10u,4)&0xffffffcfu,4);
    if (m.error) return m.error;
    *out=1;return 0;
}
int frame_entity_queue(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t destroy,
                         frame_pool_system_alloc allocate,frame_pool_system_free release,
                         void *system_ctx,uint32_t work_limit)
{
    physics_memory m={read,write,ctx,0};uint32_t object,flags,head;
    if (!read || !write || !work_limit) return -1;
    object=entity_object(&m,entity);flags=pm_load(&m,object+0x10u,4);
    if (destroy && (flags&32u)) return m.error;
    pm_store(&m,object+0x10u,flags|32u,4);head=pm_load(&m,destroy ? 0x55b6b8u : 0x55b6bcu,4);
    if (m.error) return m.error;
    return frame_list_prepend_unique(read,write,ctx,entity,head,allocate,release,system_ctx,work_limit);
}
int frame_entity_cancel_notification(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                                       frame_pool_system_free release,void *release_ctx,uint32_t work_limit)
{
    physics_memory m={read,write,ctx,0};uint32_t object,head;
    if (!read || !write || !work_limit) return -1;
    object=entity_object(&m,entity);pm_store(&m,object+0x10u,pm_load(&m,object+0x10u,4)&0xffffffdfu,4);
    head=pm_load(&m,0x55b6bcu,4);if (m.error) return m.error;
    return frame_list_remove_value(read,write,ctx,entity,head,release,release_ctx,work_limit);
}
int frame_entity_reset_transform(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity)
{
    physics_memory m={read,write,ctx,0};uint32_t object;unsigned i;
    if (!read || !write) return -1;
    object=entity_object(&m,entity);
    if (object) {
        pm_store(&m,object+0x14u,object+0x18u,4);
        for (i=0;i<12;i++) pm_store(&m,object+0x18u+i*4u,pm_load(&m,0x532888u+i*4u,4),4);
    }
    return m.error;
}

static int class_dispatch(frame_data_read read,void *ctx,uint32_t klass,uint32_t entity,uint32_t has_payload,uint32_t payload,
                             frame_native_method method,void *method_ctx,uint32_t *out)
{
    physics_memory m={read,0,ctx,0};uint32_t slot,target=0,result=0;int status;
    if (!read || !out) return -1;
    slot=pm_load(&m,0x55c698u,4);
    if (pm_signed(slot)<=pm_i32(&m,klass+0x1cu))
        target=pm_load(&m,pm_load(&m,klass+0x24u,4)+slot*4u,4);
    if (m.error) return m.error;
    if (target) {
        if (!method) return -5;
        status=method(method_ctx,target,entity,has_payload,payload,&result);if (status) return status;
    }
    *out=result;return 0;
}
int frame_class_dispatch(frame_data_read read,void *ctx,uint32_t klass,uint32_t entity,
                           frame_native_method method,void *method_ctx,uint32_t *out)
{ return class_dispatch(read,ctx,klass,entity,0,0,method,method_ctx,out); }
int frame_class_dispatch_payload(frame_data_read read,void *ctx,uint32_t klass,uint32_t entity,uint32_t payload,
                                   frame_native_method method,void *method_ctx,uint32_t *out)
{ return class_dispatch(read,ctx,klass,entity,1,payload,method,method_ctx,out); }

int frame_entity_dispatch(frame_data_read read,frame_data_write write,void *ctx,
                            uint32_t entity,uint32_t descriptor,uint32_t has_payload,uint32_t payload,
                            frame_native_method method,void *method_ctx,uint32_t *out)
{
    physics_memory mem={read,write,ctx,0},*m=&mem;
    uint32_t slot,table,saved,record,klass,target,result=0;
    int status;
    if (!read || !write || !out) return -1;
    slot=pm_load(m,descriptor+0x1cu,4);table=pm_load(m,0x55c694u,4);
    saved=pm_load(m,0x55c698u,4);pm_store(m,0x55c698u,slot,4);
    record=pm_load(m,table+entity*8u,4);klass=pm_load(m,record,4);
    if (m->error) return m->error;
    if (pm_signed(slot)>pm_i32(m,klass+0x1cu)) {
        if (m->error) return m->error;
        *out=0;return 0; /* Original leaves current-slot changed on this exit. */
    }
    table=pm_load(m,klass+0x24u,4);target=pm_load(m,table+slot*4u,4);
    if (m->error) return m->error;
    if (target) {
        if (!method) return -5;
        status=method(method_ctx,target,entity,!!has_payload,payload,&result);
        if (status) return status;
    }
    pm_store(m,0x55c698u,saved,4);
    if (m->error) return m->error;
    *out=result;return 0;
}

int frame_entities_tick(frame_data_read read,frame_data_write write,void *ctx,
                         frame_native_method method,void *method_ctx,uint32_t work_limit)
{
    physics_memory mem={read,write,ctx,0},*m=&mem;
    uint32_t head,node,entity,descriptor,result,work=0;
    int status;
    if (!read || !write || !work_limit) return -1;
    head=pm_load(m,0x55b6a8u,4);node=pm_load(m,head,4);
    while (node!=head) {
        if (m->error) return m->error;
        if (work++>=work_limit) return -3;
        pm_store(m,0x55b6c0u,pm_load(m,node,4),4);
        descriptor=pm_load(m,0x545eb8u,4);entity=pm_load(m,node+8u,4);
        if (m->error) return m->error;
        status=frame_entity_dispatch(read,write,ctx,entity,descriptor,0,0,method,method_ctx,&result);
        if (status) return status;
        node=pm_load(m,0x55b6c0u,4);head=pm_load(m,0x55b6a8u,4);
    }
    pm_store(m,0x55b6c0u,0,4);
    return m->error;
}
int frame_entity_dispatch_pair(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t descriptor,
                                 uint32_t payload,uint32_t second_payload,frame_native_method_pair method,void *method_ctx,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t saved,slot,record,klass,target,result=0;int status;
    if (!read || !write || !out) return -1;
    slot=pm_load(&m,descriptor+28u,4);saved=pm_load(&m,0x55c698u,4);pm_store(&m,0x55c698u,slot,4);
    record=pm_load(&m,pm_load(&m,0x55c694u,4)+entity*8u,4);klass=pm_load(&m,record,4);
    if (m.error) return m.error;
    if (pm_signed(slot)>pm_i32(&m,klass+28u)) { if (m.error) return m.error;*out=0;return 0; }
    target=pm_load(&m,pm_load(&m,klass+36u,4)+slot*4u,4);if (m.error) return m.error;
    if (target) {
        if (!method) return -5;
        status=method(method_ctx,target,entity,payload,second_payload,&result);if (status) return status;
    }
    pm_store(&m,0x55c698u,saved,4);if (m.error) return m.error;*out=result;return 0;
}

int frame_entity_activate(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                           frame_native_method method,void *method_ctx,uint32_t *out)
{
    physics_memory mem={read,write,ctx,0},*m=&mem;
    uint32_t descriptor,result;
    int status;
    if (!read || !write || !out) return -1;
    descriptor=pm_load(m,0x545f3cu,4);
    if (m->error) return m->error;
    status=frame_entity_dispatch(read,write,ctx,entity,descriptor,0,0,method,method_ctx,&result);
    if (status) return status;
    *out=1;return 0;
}
int frame_entity_release_id(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity)
{
    physics_memory mem={read,write,ctx,0},*m=&mem;
    if (!read || !write) return -1;
    pm_store(m,pm_load(m,0x55c694u,4)+entity*8u,0,4);
    pm_store(m,pm_load(m,0x55c694u,4)+entity*8u+4u,pm_load(m,0x536cbcu,4),4);
    pm_store(m,0x536cbcu,entity,4);return m->error;
}
int frame_entity_destroy(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                           frame_native_method method,void *method_ctx,
                           frame_pool_system_free release,void *release_ctx)
{
    physics_memory mem={read,write,ctx,0},*m=&mem;
    uint32_t record,descriptor,result;
    int status;
    if (!read || !write) return -1;
    if (entity<pm_load(m,0x55c690u,4)) {
        record=pm_load(m,pm_load(m,0x55c694u,4)+entity*8u,4);
        if (record && !m->error) {
            descriptor=pm_load(m,0x545ef4u,4);
            if (m->error) return m->error;
            status=frame_entity_dispatch(read,write,ctx,entity,descriptor,0,0,method,method_ctx,&result);
            if (status) return status;
            descriptor=pm_load(m,0x545ed4u,4);
            if (m->error) return m->error;
            status=frame_entity_dispatch(read,write,ctx,entity,descriptor,0,0,method,method_ctx,&result);
            if (status) return status;
            status=frame_pool_release(read,write,ctx,record,8,release,release_ctx);
            if (status) return status;
            pm_store(m,0x55c68cu,pm_load(m,0x55c68cu,4)-1u,4);
        }
    }
    return m->error;
}
int frame_entities_drain_queues(frame_data_read read,frame_data_write write,void *ctx,
                                  frame_native_method method,void *method_ctx,
                                  frame_pool_system_free release,void *release_ctx,uint32_t work_limit)
{
    physics_memory mem={read,write,ctx,0},*m=&mem;
    unsigned phase;
    uint32_t entity,descriptor,result,head,work=0;
    int status;
    if (!read || !write || !work_limit) return -1;
    for (phase=0;phase<2;phase++) for (;;) {
        if (work++>=work_limit) return -3;
        head=pm_load(m,phase ? 0x55b6b8u : 0x55b6bcu,4);
        if (m->error) return m->error;
        status=frame_list_pop(read,write,ctx,head,release,release_ctx,&entity);
        if (status) return status;
        if (!entity) break;
        descriptor=pm_load(m,0x545f34u,4);
        if (m->error) return m->error;
        status=frame_entity_dispatch(read,write,ctx,entity,descriptor,0,0,method,method_ctx,&result);
        if (status) return status;
        if (phase) {
            status=frame_entity_destroy(read,write,ctx,entity,method,method_ctx,release,release_ctx);
            if (status) return status;
        }
    }
    return m->error;
}
