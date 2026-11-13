#include "frame_mine.h"
#include "frame_text.h"
#include "physics_memory.h"
#include "physics_vector.h"
#include "frame_camera.h"
static uint32_t object_of(physics_memory *m,uint32_t id)
{ uint32_t r=pm_load(m,pm_load(m,0x55c694u,4)+id*8u,4);return r ? pm_load(m,r+4u,4) : 0; }
#define TRY(x) do { if (m.error) return m.error;status=(x);if (status) return status; } while (0)
int frame_mine_align(frame_data_read read,frame_data_write write,void *ctx,uint32_t o,const physics_trig *trig)
{
    physics_memory m={read,write,ctx,0};uint32_t matrix,i,j;int32_t height,normal[3],local[3],pitch,roll;int status;
    if (!read || !write || !trig) return -1;
    status=physics_terrain_normal_height(read,ctx,pm_load(&m,0x55ae60u,4),pm_i32(&m,o),pm_i32(&m,o+8u),&height,normal);if (status<0) return status;if (!status) return -4;
    matrix=pm_load(&m,o+20u,4);if (!matrix) return m.error ? m.error : -4;
    for (i=0;i<3u;i++) { local[i]=0;for (j=0;j<3u;j++) local[i]=pm_add(local[i],pm_mul(normal[j],pm_i32(&m,matrix+(i*3u+j)*4u)));local[i]=pm_sar(local[i],12); }
    TRY(physics_angle_lookup(local[1],local[2],trig,&pitch));TRY(physics_angle_lookup(pm_neg(local[0]),local[1],trig,&roll));
    TRY(frame_matrix_rotate(read,write,ctx,matrix,0,(uint32_t)pm_sub(pitch,16384),trig));TRY(frame_matrix_rotate(read,write,ctx,matrix,2,(uint32_t)pm_neg(roll),trig));return m.error;
}
int frame_mine_enforce_limit(frame_data_read read,frame_data_write write,void *ctx,const frame_entity_factory_services *e)
{
    physics_memory m={read,write,ctx,0};uint32_t n,head,entity,d,unused;
    if (!read || !write || !e) return -1;
    if (pm_load(&m,0x58e7acu,4)) return m.error ? m.error : -5;
    n=pm_load(&m,0x53e9bcu,4)+1u;pm_store(&m,0x53e9bcu,n,4);if (n<=30u) return m.error;
    head=pm_load(&m,0x53e9b8u,4);entity=pm_load(&m,pm_load(&m,head,4)+8u,4);d=pm_load(&m,0x545ed0u,4);if (m.error) return m.error;
    return frame_entity_dispatch(read,write,ctx,entity,d,0,0,e->method,e->method_ctx,&unused);
}
int frame_mine_clear(frame_data_read read,frame_data_write write,void *ctx,const frame_entity_factory_services *e)
{
    physics_memory m={read,write,ctx,0};uint32_t head;int status;
    if (!read || !write || !e || !e->work_limit) return -1;
    head=pm_load(&m,0x53e9b8u,4);TRY(frame_list_clear(read,write,ctx,head,e->release,e->system_ctx,e->work_limit));pm_store(&m,0x53e9bcu,0,4);return m.error;
}
int frame_mine_construct(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t parser,const frame_entity_factory_services *e,const physics_trig *trig)
{
    physics_memory m={read,write,ctx,0};uint32_t o,parent,unused,found;int status;
    if (!read || !write || !e || !e->work_limit) return -1;
    o=object_of(&m,entity);parent=pm_load(&m,0x544f80u,4);TRY(frame_class_dispatch_payload(read,ctx,parent,entity,parser,e->method,e->method_ctx,&unused));
    TRY(frame_mine_align(read,write,ctx,o,trig));TRY(frame_text_find(read,write,ctx,parser,0x530494u,e->work_limit,&found));return m.error;
}
int frame_mine_parent(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *e)
{
    physics_memory m={read,write,ctx,0};uint32_t parent,unused;
    if (!read || !write || !e) return -1;
    parent=pm_load(&m,0x544f80u,4);if (m.error) return m.error;return frame_class_dispatch(read,ctx,parent,entity,e->method,e->method_ctx,&unused);
}
int frame_mine_remove(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *e)
{
    physics_memory m={read,write,ctx,0};uint32_t head;int status;
    if (!read || !write || !e || !e->work_limit) return -1;
    if (pm_load(&m,0x58e7acu,4)) return m.error ? m.error : -5;
    TRY(frame_mine_parent(read,write,ctx,entity,e));head=pm_load(&m,0x53e9b8u,4);
    TRY(frame_list_remove_value(read,write,ctx,entity,head,e->release,e->system_ctx,e->work_limit));pm_store(&m,0x53e9bcu,pm_load(&m,0x53e9bcu,4)-1u,4);return m.error;
}
int frame_mine_mark_inactive(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity)
{
    physics_memory m={read,write,ctx,0};uint32_t o;if (!read || !write) return -1;
    o=object_of(&m,entity);if (o) { pm_store(&m,o+0xd2u,0,4);if (!pm_load(&m,o+0xcau,4)) pm_store(&m,o+0xcau,1,4); }return m.error;
}
int frame_mine_spawn(frame_data_read read,frame_data_write write,void *ctx,uint32_t owner,uint32_t repeat,uint32_t type,const frame_entity_factory_services *e,const physics_trig *trig,frame_effect_sink feedback,void *feedback_ctx,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t tank,entity,o,klass,d,unused,shape,i,head;int32_t height;int status;
    if (!read || !write || !e || !e->work_limit || !out) return -1;
    if (pm_load(&m,0x58e7acu,4)) return m.error ? m.error : -5;
    tank=object_of(&m,owner);klass=pm_load(&m,0x53e9acu,4);TRY(frame_entity_create(read,write,ctx,klass,e,&entity));*out=entity;
    if (!entity) return m.error;
    o=object_of(&m,entity);if (!o) return m.error;
    pm_store(&m,o+0xaau,owner,4);pm_store(&m,o+0xaeu,type,4);pm_store(&m,o,pm_load(&m,tank+120u,4),4);pm_store(&m,o+8u,pm_load(&m,tank+128u,4),4);
    if (m.error) return m.error;
    status=physics_terrain_height(read,ctx,pm_load(&m,0x55ae60u,4),pm_i32(&m,o),pm_i32(&m,o+8u),&height);if (status<0) return status;pm_write32(&m,o+4u,height);
    pm_store(&m,o+20u,o+24u,4);pm_store(&m,o+76u,0x9000,2);pm_store(&m,o+0xb9u,pm_load(&m,0x590564u,4),4);pm_store(&m,o+0xb5u,pm_load(&m,0x590564u,4),4);
    d=pm_load(&m,0x545f44u,4);TRY(frame_entity_dispatch(read,write,ctx,entity,d,0,0,e->method,e->method_ctx,&unused));
    pm_store(&m,o+72u,pm_load(&m,type ? 0x53e9b4u : 0x53e9b0u,4),4);
    if (repeat) { pm_store(&m,o+0xb3u,1,1);pm_store(&m,o+0xb2u,1,1);if (pm_load(&m,tank+0xde5u,1)) TRY(frame_feedback_mine(read,write,ctx,feedback,feedback_ctx)); }
    shape=pm_load(&m,o+72u,4);for (i=0;i<6u;i++) pm_write32(&m,o+96u+i*4u,pm_add(pm_mul(pm_i16(&m,shape+28u+i*2u),8),i<3u ? -1536 : 1536));
    TRY(frame_mine_align(read,write,ctx,o,trig));TRY(frame_entity_activate(read,write,ctx,entity,e->method,e->method_ctx,&unused));
    head=pm_load(&m,0x53e9b8u,4);TRY(frame_list_append(read,write,ctx,entity,head,e->allocate,e->release,e->system_ctx,e->work_limit,&unused));TRY(frame_mine_enforce_limit(read,write,ctx,e));return m.error;
}
int frame_mine_contact(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t vehicle,const frame_entity_factory_services *e,const frame_explosion_services *effects,frame_effect_sink feedback,void *feedback_ctx)
{
    physics_memory m={read,write,ctx,0};uint32_t o,tank,frame,type,d,unused,bank,previous,camera;int status;
    if (!read || !write || !e || !e->work_limit) return -1;
    if (pm_load(&m,0x58e7acu,4)) return m.error ? m.error : -5;
    o=object_of(&m,entity);tank=object_of(&m,vehicle);
    if (!pm_load(&m,o+0xb4u,1)) {
        frame=pm_load(&m,0x590564u,4);
        if (frame!=pm_load(&m,o+0xb5u,4) && pm_load(&m,o+0xb9u,4)!=frame-1u) pm_store(&m,o+0xb4u,1,1);
        else pm_store(&m,o+0xb9u,frame,4);
        return m.error;
    }
    type=pm_load(&m,o+0xaeu,4);TRY(frame_explosion_spawn_mine(read,write,ctx,o,vehicle,type==1u ? 4u : 3u,e,effects,&unused));
    frame_entity_sound(e,0x40f720u,o,0,0);
    if (pm_i32(&m,tank+0xdafu)<1) {
        d=pm_load(&m,0x545f18u,4);TRY(frame_entity_dispatch(read,write,ctx,vehicle,d,1,o,e->method,e->method_ctx,&unused));
        if (pm_load(&m,tank+0xde5u,1)) {
            camera=pm_load(&m,0x53cc38u+(uint32_t)pm_i8(&m,tank+0xdf0u)*4u,4);TRY(frame_camera_shake(read,write,ctx,camera,600,1500));
            type=pm_load(&m,o+0xaeu,4);TRY(frame_feedback_collision(read,write,ctx,type,1,feedback,feedback_ctx));
        }
        if (pm_load(&m,o+0xaeu,4)==1u) {
            bank=pm_load(&m,tank+0xde5u,1);pm_store(&m,tank+0xe0bu,6000,2);pm_store(&m,tank+0xe0du,pm_load(&m,tank+0x30au,2),2);pm_store(&m,tank+0xe08u,1,1);
            previous=pm_load(&m,0x58eebcu,4);if (!bank && previous) { previous=object_of(&m,previous);pm_store(&m,previous+0xe08u,0,1); }
            pm_store(&m,0x58eebcu+bank*160u,vehicle,4);
        }
    }
    d=pm_load(&m,0x545ed0u,4);TRY(frame_entity_dispatch(read,write,ctx,entity,d,0,0,e->method,e->method_ctx,&unused));return m.error;
}
int frame_mine_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *e,const physics_trig *trig,frame_effect_sink feedback,void *feedback_ctx)
{
    physics_memory m={read,write,ctx,0};uint32_t o,unused,d;int32_t elapsed;int status;
    if (!read || !write || !e || !e->work_limit) return -1;
    if (pm_load(&m,0x58e7acu,4)) return m.error ? m.error : -5;
    o=object_of(&m,entity);elapsed=pm_add(pm_i32(&m,o+0xbdu),pm_i32(&m,0x5904fcu));pm_write32(&m,o+0xbdu,elapsed);
    if (!pm_load(&m,o+0xb3u,1) || pm_i8(&m,o+0xb2u)>2) {
        if (elapsed>20000) { d=pm_load(&m,0x545ed0u,4);TRY(frame_entity_dispatch(read,write,ctx,entity,d,0,0,e->method,e->method_ctx,&unused)); }
    } else if (elapsed>250) {
        pm_store(&m,o+0xbdu,0,4);pm_store(&m,o+0xb2u,pm_load(&m,o+0xb2u,1)+1u,1);
        TRY(frame_mine_spawn(read,write,ctx,pm_load(&m,o+0xaau,4),0,pm_load(&m,o+0xaeu,4),e,trig,feedback,feedback_ctx,&unused));
        frame_entity_sound(e,0x40f5b0u,o,1u,0);
    }
    return m.error;
}
#undef TRY
