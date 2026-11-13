#include "frame_player_lap.h"
#include "frame_progress.h"
#include "frame_spatial.h"
#include "physics_memory.h"

static uint32_t entity_object(physics_memory *m,uint32_t entity)
{
    uint32_t record=pm_load(m,pm_load(m,0x55c694u,4)+entity*8u,4);
    return record ? pm_load(m,record+4u,4) : 0;
}
static int restore_objects(physics_memory *m,const frame_player_lap_services *s)
{
    uint32_t head=pm_load(m,0x53d148u,4),node=pm_load(m,head,4),work=0;
    while (node!=head) {
        uint32_t id,object,near=0;int status;
        if (m->error) return m->error;
        if (work++>=s->work_limit) return -3;
        id=pm_load(m,node+8u,4);object=entity_object(m,id);
        if (pm_load(m,0x54b480u,4)) {
            status=frame_entity_near_camera(m->read,m->ctx,object,0,19200,&near);if (status) return status;
            if (!near) { status=frame_entity_near_camera(m->read,m->ctx,object,1,19200,&near);if (status) return status; }
        }
        if (!near) { status=frame_entity_near_camera(m->read,m->ctx,object,0,19200,&near);if (status) return status; }
        if (!near) {
            if (m->error) return m->error;
            status=frame_object_restore(m->read,m->write,m->ctx,id,s->allocate,s->release,s->system_ctx,s->work_limit);
            if (status) return status;
        }
        node=pm_load(m,node,4);head=pm_load(m,0x53d148u,4);
    }
    return m->error;
}
int frame_player_lap_update(frame_data_read read,frame_data_write write,void *ctx,
                              uint32_t player,uint32_t entity,const frame_player_lap_services *s)
{
    physics_memory mem={read,write,ctx,0},*m=&mem;
    frame_checkpoint_result checkpoint;int status;
    if (!read || !write || !s || !s->work_limit) return -1;
    if (pm_load(m,0x52ee9cu,4)==2 || pm_load(m,0x52ee9cu,4)==4) return m->error;
    status=frame_checkpoint_query(read,ctx,entity,pm_load(m,player+0x790u,4),0,&checkpoint);
    if (status) return status;
    if (!checkpoint.next_entity) return m->error;
    pm_store(m,player+0x790u,checkpoint.next_entity,4);
    pm_store(m,player+0x794u,pm_load(m,player+0x794u,4)+1u,4);
    if (checkpoint.finishes_lap) {
        if (pm_i16(m,player+0xddau)==pm_i32(m,player+0xcb6u)) {
            uint32_t events=1;
            if (m->error) return m->error;
            status=frame_lap_record(read,write,ctx,entity);if (status) return status;
            pm_store(m,player+0xddau,pm_load(m,player+0xddau,2)+1u,2);
            if (pm_load(m,0x52ee9cu,4)==3 && pm_i16(m,player+0xddau)==3) {
                uint32_t other=entity_object(m,pm_load(m,0x53e8bcu-(uint32_t)pm_i8(m,player+0xdf0u)*4u,4));
                pm_store(m,player+0xdddu,pm_i16(m,other+0xddau)==3 ? 2u : 1u,1);
            }
            if (pm_load(m,0x52ee9cu,4)==5 && pm_i16(m,player+0xddau)==3) {
                if (m->error) return m->error;
                status=frame_network_race_finished(read,write,ctx,s->network,s->network_ctx,s->workspace,s->capacity,s->work_limit);
                if (status) return status;
            }
            if (pm_load(m,0x54b480u,4)) {
                uint32_t first=pm_load(m,0x53e8b8u,4),second=pm_load(m,0x53e8bcu,4);
                uint32_t other=entity_object(m,entity==first ? second : entity==second ? first : checkpoint.finishes_lap);
                if (other && pm_i16(m,player+0xddau)<=pm_i16(m,other+0xddau)) events=0;
            }
            if (m->error) return m->error;
            if (events) {
                if (pm_load(m,0x52ee9cu,4)==5 && pm_i32(m,0x52ed90u)>7)
                    status=frame_lap_object_events_special(read,write,ctx,player,s->method,s->method_ctx,s->allocate,s->release,s->system_ctx,s->work_limit);
                else
                    status=frame_lap_object_events(read,write,ctx,player,s->method,s->method_ctx,s->allocate,s->release,s->system_ctx,s->work_limit);
                if (status) return status;
                status=restore_objects(m,s);if (status) return status;
            }
        } else pm_store(m,player+0xddau,pm_load(m,player+0xddau,2)+1u,2);
    }
    if (pm_load(m,0x52ee9cu,4)==3) {
        uint32_t target=entity_object(m,checkpoint.next_entity);
        if (pm_load(m,target+0x9bu,1)) {
            uint32_t index=pm_load(m,player+0x1112u,4)+1u;
            uint32_t destination,work=0;
            pm_store(m,player+0x1112u,index,4);
            pm_store(m,pm_load(m,player+0x110eu,4)+index*4u,0,4);
            while (pm_signed(work)<pm_i32(m,player+0xcb6u)) {
                uint32_t value;
                if (m->error) return m->error;
                if (work>=s->work_limit) return -3;
                destination=pm_load(m,player+0x110eu,4)+pm_load(m,player+0x1112u,4)*4u;
                value=pm_load(m,destination,4)+pm_load(m,player+0xcbeu+work*7u,4);
                pm_store(m,destination,value,4);work++;
            }
            destination=pm_load(m,player+0x110eu,4)+pm_load(m,player+0x1112u,4)*4u;
            pm_store(m,destination,pm_load(m,destination,4)+pm_load(m,player+0xcbau,4),4);
            pm_store(m,0x5904e8u,2000,4);pm_store(m,0x5904e4u,0,4);
        }
    }
    return m->error;
}
