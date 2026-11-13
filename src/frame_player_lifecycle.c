#include "frame_player_lifecycle.h"
#include "frame_progress.h"
#include "frame_shape.h"
#include "frame_scene_effects.h"
#include "frame_text.h"
#include "frame_shatter.h"
#include "physics_pose.h"
#include "physics_memory.h"
#include "frame_controls.h"

static uint32_t object_of(physics_memory *m,uint32_t id)
{
    uint32_t record=pm_load(m,pm_load(m,0x55c694u,4)+id*8u,4);
    return record ? pm_load(m,record+4u,4) : 0;
}
int frame_player_finish_owner_initialize(frame_data_read read,frame_data_write write,void *ctx,uint32_t id,const frame_entity_factory_services *e,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t o,parent,unused;int status;if (!read || !write || !e || !out) return -1;
    o=object_of(&m,id);parent=pm_load(&m,0x545fdcu,4);if (m.error) return m.error;
    status=frame_class_dispatch(read,ctx,parent,id,e->method,e->method_ctx,&unused);if (status) return status;
    unused=pm_load(&m,o+0x10f3u,4);pm_store(&m,o+0xde5u,0,1);pm_store(&m,o+0x10f3u,unused|32u,4);if (m.error) return m.error;*out=unused|32u;return 0;
}
int frame_player_finish_owner_create(frame_data_read read,frame_data_write write,void *ctx,const frame_entity_factory_services *e)
{
    physics_memory m={read,write,ctx,0};uint32_t source,target,id,cup,i;int status;if (!read || !write || !e) return -1;
    source=object_of(&m,pm_load(&m,0x53e8b8u,4));id=pm_load(&m,0x53b698u,4);if (m.error) return m.error;
    status=frame_entity_create(read,write,ctx,id,e,&id);if (status) return status;pm_store(&m,0x53b69cu,id,4);target=object_of(&m,id);cup=pm_load(&m,0x53d18cu,4);if (cup>2u) return m.error ? m.error : -4;
    pm_store(&m,target+0xde6u,1,1);pm_store(&m,target+0x10f7u,0x53b6a0u+cup*1820u,4);pm_store(&m,source+0x10bau,pm_load(&m,source+0x66au,2),2);
    for (i=0;i<7u;i++) pm_store(&m,target+0x5cu+i*4u,pm_load(&m,source+0x5cu+i*4u,4),4);
    if (m.error) return m.error;
    return frame_entity_queue(read,write,ctx,id,0,e->allocate,e->release,e->system_ctx,e->work_limit);
}
int frame_player_finish_owner_activate(frame_data_read read,frame_data_write write,void *ctx,const frame_entity_factory_services *e)
{
    physics_memory m={read,write,ctx,0};uint32_t id,o,unused;int status;if (!read || !write || !e) return -1;
    if (!pm_load(&m,0x53e990u,4)) return m.error;
    id=pm_load(&m,0x53b69cu,4);o=object_of(&m,id);if (m.error) return m.error;
    status=frame_entity_activate(read,write,ctx,id,e->method,e->method_ctx,&unused);if (status) return status;
    pm_store(&m,0x53e990u,0,4);pm_store(&m,o+0xd0au,0,1);return m.error;
}
int frame_player_horn(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity)
{
    physics_memory m={read,write,ctx,0};uint32_t o,first,second,code,key=0,i,other;int status,down=0;
    if (!read || !write) return -1;
    o=object_of(&m,entity);
    if (pm_load(&m,0x52ee9cu,4)==4 && pm_load(&m,o+0xde5u,1) && pm_load(&m,0x543d40u,4)==1) return m.error;
    if (!pm_load(&m,o+0xdedu,1)) return m.error;
    first=pm_load(&m,0x53e8b8u,4);second=pm_load(&m,0x53e8bcu,4);
    if (entity==first || entity==second) {
        if (entity==first) {
            code=pm_load(&m,0x592024u,4);if (m.error) return m.error;
            status=frame_control_key(read,ctx,code,&key);if (status) return status;down=key==1;
            if (!down && pm_load(&m,0x53ccdcu,4)) {
                code=pm_load(&m,0x592924u,4);
                if (code) { status=frame_control_key(read,ctx,code,&key);if (status) return status;down=key==1; }
            }
        }
        pm_store(&m,o+0xd03u,(uint32_t)down,1);
    }
    if (!pm_load(&m,o+0xd03u,1)) pm_store(&m,o+0xd04u,0,1);
    else if (!pm_load(&m,o+0xd04u,1)) {
        pm_store(&m,o+0xd04u,1,1);
        if (entity==pm_load(&m,0x53e8b8u,4)) pm_store(&m,o+0xd0du,1000,2);
    }
    if (entity==pm_load(&m,0x53e8b8u,4) && pm_i16(&m,o+0xd0du)) {
        if (pm_i32(&m,0x5904fcu)<pm_i16(&m,o+0xd0du)) pm_store(&m,o+0xd0du,(uint32_t)pm_sub(pm_i16(&m,o+0xd0du),pm_i32(&m,0x5904fcu)),2);
        else {
            for (i=0;i<7u;i++) {
                other=object_of(&m,pm_load(&m,0x53e8c0u+i*4u,4));
                if (other && pm_load(&m,other+0xdedu,1)) pm_write32(&m,other+0x10c6u,pm_add(pm_i32(&m,other+0x10c6u),2000));
            }
            pm_store(&m,o+0xd0du,0,2);
        }
    }
    return m.error;
}
int frame_tank_restore_route(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const physics_trig *trig,uint32_t limit,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t o,initial,head,node,saved,current,previous,candidate,blocked,source,target,i,j,used=0;int32_t v[3],local[3],angle;int status;
    static const uint32_t clear[]={0x94,0x98,0x9c,0x58,0x54,0x50,0xa0,0xa4,0xbc,0xb8,0xb4,0xc0};
    if (!read || !write || !trig || !limit || !out) return -1;
    o=object_of(&m,entity);initial=pm_load(&m,o+0x790u,4);saved=initial;head=pm_load(&m,0x5468a0u,4);node=pm_load(&m,head,4);
    while (node!=head && pm_load(&m,node+8u,4)!=initial) {
        if (m.error) return m.error;
        if (used++>=limit) return -3;
        node=pm_load(&m,node,4);
    }
    if (node!=head) {
        do {
            if (m.error) return m.error;
            if (used++>=limit) return -3;
            previous=pm_load(&m,node+4u,4);current=pm_load(&m,node+8u,4);
            candidate=previous==head ? pm_load(&m,pm_load(&m,head+4u,4)+8u,4) : pm_load(&m,previous+8u,4);
            pm_store(&m,o+0x790u,candidate,4);
            if (pm_i32(&m,object_of(&m,current)+0x90u)!=-1) saved=current;
            pm_write32(&m,o+0x794u,pm_sub(pm_i32(&m,o+0x794u),1));node=previous;
            if (node==head) node=pm_load(&m,node+4u,4);
            target=object_of(&m,pm_load(&m,o+0x790u,4));
            if (pm_i32(&m,target+0x90u)==-1) continue;
            blocked=pm_load(&m,0x546740u,4);if (m.error) return m.error;
            status=frame_list_contains(read,ctx,pm_load(&m,o+0x790u,4),blocked,limit,&blocked);if (status) return status;
            if (!blocked) break;
        } while (1);
        source=object_of(&m,initial);target=object_of(&m,pm_load(&m,o+0x790u,4));
        if (pm_i32(&m,source+0x90u)<pm_i32(&m,target+0x90u)) pm_store(&m,o+0xddau,pm_load(&m,o+0xddau,2)-1u,2);
    }
    source=object_of(&m,pm_load(&m,o+0x790u,4));target=object_of(&m,saved);
    for (i=0;i<3u;i++) pm_write32(&m,o+0x78u+i*4u,pm_add(pm_i32(&m,source+i*4u),i==1u ? 6144 : 0));
    for (i=0;i<3u;i++) v[i]=pm_sub(pm_i32(&m,target+i*4u),pm_i32(&m,source+i*4u));
    for (i=0;i<3u;i++) { local[i]=0;for (j=0;j<3u;j++) local[i]=pm_add(local[i],pm_mul(v[j],pm_i32(&m,o+0x2b2u+(i*3u+j)*4u)));local[i]=pm_sar(local[i],12); }
    while (pm_abs(local[0])>32768 || pm_abs(local[1])>32768 || pm_abs(local[2])>32768) {
        if (used++>=limit) return -3;
        for (i=0;i<3u;i++) local[i]=pm_sar(local[i],1);
    }
    if (m.error) return m.error;
    status=physics_angle_lookup(local[0],local[2],trig,&angle);if (status) return status;
    status=frame_matrix_rotate(read,write,ctx,o+0x2b2u,1,(uint32_t)pm_neg(angle),trig);if (status) return status;
    status=physics_pose_update(read,write,ctx,o,trig);if (status) return status;
    for (i=0;i<sizeof(clear)/sizeof(clear[0]);i++) pm_store(&m,o+clear[i],0,4);
    if (m.error) return m.error;
    *out=pm_load(&m,source+4u,4);return m.error;
}
int frame_player_construct(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t parser,const frame_entity_factory_services *s,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t object,parent,unused,found,slot=0,more,descriptor,racer,arena,i;int status;
    if (!read || !write || !s || !s->work_limit || !out) return -1;
    object=object_of(&m,entity);
    if (pm_load(&m,0x52ee9cu,4)==2u) pm_store(&m,object+0x10c2u,pm_load(&m,0x531ba4u,4),4);
    parent=pm_load(&m,0x545fdcu,4);if (m.error) return m.error;
    status=frame_class_dispatch_payload(read,ctx,parent,entity,parser,s->method,s->method_ctx,&unused);if (status) return status;
    status=frame_text_find_name(read,write,ctx,parser,(const unsigned char *)":player",8,s->work_limit,&found);if (status) return status;
    if (found) {
        status=frame_text_read_value(read,write,ctx,parser,s->work_limit,&slot,&more);if (status) return status;
        if (!more) return -4;
        if (slot<2u) {
            pm_store(&m,object+0xdf0u,slot,1);pm_store(&m,0x53e8b8u+slot*4u,entity,4);
            descriptor=pm_load(&m,0x545ed8u,4);if (m.error) return m.error;
            status=frame_entity_dispatch(read,write,ctx,entity,descriptor,1,slot ? 0x530400u : 0x5303c0u,s->method,s->method_ctx,&unused);if (status) return status;
            pm_store(&m,object+0xddfu,pm_load(&m,slot ? 0x530444u : 0x530440u,1),1);
            racer=pm_load(&m,0x53e8b8u+slot*4u,4);descriptor=pm_load(&m,0x545ee0u,4);if (m.error) return m.error;
            status=frame_entity_dispatch(read,write,ctx,racer,descriptor,0,0,s->method,s->method_ctx,&unused);if (status) return status;
            racer=pm_load(&m,0x53e8b8u+slot*4u,4);if (m.error) return m.error;
            status=frame_camera_create_chase(read,write,ctx,racer,slot,s);if (status) return status;
            if (pm_load(&m,0x52ee9cu,4)==3u) {
                status=frame_level_allocate(read,write,ctx,48,&arena);if (status) return status;
                pm_store(&m,object+0x110eu,arena,4);
                for (i=0;i<12u;i++) pm_store(&m,arena+i*4u,0,4);
                pm_store(&m,object+0x1112u,0xffffffffu,4);
            } else pm_store(&m,object+0x110eu,0,4);
        }
    }
    if (m.error) return m.error;
    *out=entity;return 0;
}
int frame_player_destroy(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *s,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t object,parent,unused;int status;
    if (!read || !write || !s || !out) return -1;
    object=object_of(&m,entity);
    if (pm_load(&m,object+0x110eu,4)) pm_store(&m,object+0x110eu,0,4);
    parent=pm_load(&m,0x545fdcu,4);if (m.error) return m.error;
    status=frame_class_dispatch(read,ctx,parent,entity,s->method,s->method_ctx,&unused);if (status) return status;
    *out=entity;return 0;
}
int frame_ai_route_branch(frame_data_read read,frame_data_write write,void *ctx,uint32_t object,
                             uint32_t checkpoint,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t point,index,result=0;
    if (!read || !write || !out) return -1;
    point=object_of(&m,checkpoint);
    if (pm_load(&m,point+0x7cu,4)) {
        for (index=0;index<2;index++) {
            uint32_t next=object_of(&m,pm_load(&m,point+0x78u+index*4u,4));int32_t tag=pm_i8(&m,next+0x9au);
            if (tag!=-1) {
                int32_t lap=pm_i16(&m,object+0xddau);
                result=lap!=tag && (tag!=2 || lap<3) ? 1u-index : index;
                if (m.error) return m.error;
                *out=result;return 0;
            }
        }
        if (m.error) return m.error;
        result=pm_load(&m,0x55b6acu,4)*1664525u+1013904223u;pm_store(&m,0x55b6acu,result,4);result=(result>>4)&1u;
    }
    if (m.error) return m.error;
    *out=result;return 0;
}
int frame_ai_rank(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity)
{
    physics_memory m={read,write,ctx,0};uint32_t object,at;
    if (!read || !write) return -1;
    object=object_of(&m,entity);pm_store(&m,object+0xddcu,1,1);
    for (at=0x53e8e0u;at<0x53e904u;at+=4u) {
        uint32_t id=pm_load(&m,at,4);
        if (id && id!=entity) {
            uint32_t other=object_of(&m,id);
            if (pm_i32(&m,object+0xd18u)<pm_i32(&m,other+0xd18u))
                pm_store(&m,object+0xddcu,pm_load(&m,object+0xddcu,1)+1u,1);
        }
    }
    return m.error;
}
int frame_ai_neighbor(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity)
{
    physics_memory m={read,write,ctx,0};uint32_t object,nearest=0,at;int32_t best=102400000;
    if (!read || !write) return -1;
    object=object_of(&m,entity);
    for (at=0x53e8c0u;at<0x53e8dcu;at+=4u) {
        uint32_t id=pm_load(&m,at,4);
        if (id && id!=entity) {
            uint32_t other=object_of(&m,id);
            if (other) {
                int32_t dx=pm_abs(pm_sub(pm_i32(&m,object),pm_i32(&m,other)));
                if (dx<pm_i32(&m,0x592e80u)) {
                    int32_t dz=pm_abs(pm_sub(pm_i32(&m,object+8u),pm_i32(&m,other+8u)));
                    if (dz<pm_i32(&m,0x592e80u)) {
                        int32_t distance=dx>=dz ? pm_add(dx,pm_sub(dz,pm_sar(dz,1))) : pm_add(dz,pm_sub(dx,pm_sar(dx,1)));
                        if (distance<best) { best=distance;nearest=other; }
                    }
                }
            }
        }
    }
    if (!nearest) pm_store(&m,object+0x10fbu,0,4);
    else {
        int32_t dot=pm_add(pm_mul(pm_sub(pm_i32(&m,object+8u),pm_i32(&m,nearest+8u)),pm_i32(&m,object+0x2d2u)),
                           pm_mul(pm_sub(pm_i32(&m,object),pm_i32(&m,nearest)),pm_i32(&m,object+0x2cau)));
        pm_store(&m,object+0x10fbu,dot<=0 ? 2u : 1u,4);
    }
    pm_store(&m,object+0x10f3u,pm_load(&m,object+0x10f3u,4)|8u,4);return m.error;
}
int frame_ai_spacing_throttle(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity)
{
    physics_memory m={read,write,ctx,0};uint32_t object,state,configuration;int32_t threshold;
    if (!read || !write) return -1;
    object=object_of(&m,entity);state=pm_load(&m,object+0x10fbu,4);
    if (state==1 || state==2) {
        configuration=pm_load(&m,object+0x10f7u,4)+4u+pm_load(&m,0x52ed94u,4)*4u;
        threshold=pm_sar(pm_mul(pm_i32(&m,configuration),pm_i32(&m,state==1 ? 0x592e8cu : 0x592e90u)),
                         pm_load(&m,state==1 ? 0x592e88u : 0x592e94u,4));
        if (state==1) pm_store(&m,object+0x94u,pm_i32(&m,object+0xecu)<threshold ? 0xffffu : 0u,4);
        else if (pm_i32(&m,object+0xecu)>threshold) pm_store(&m,object+0x94u,0,4);
    }
    return m.error;
}
static void retire_light(physics_memory *m,uint32_t id)
{
    if (!m->error) m->error=frame_light_release(m->read,m->write,m->ctx,id);
}
int frame_player_takeover(frame_data_read read,frame_data_write write,void *ctx,
                             const frame_entity_factory_services *s)
{
    physics_memory m={read,write,ctx,0};uint32_t source,target,i,at,id,head,result,descriptor,work=0;int status;
    if (!read || !write || !s || !s->work_limit) return -1;
    source=object_of(&m,pm_load(&m,0x53e8b8u,4));target=object_of(&m,pm_load(&m,0x53b69cu,4));
    for (i=0;i<0x41eu;i++) {
        if (work++>=s->work_limit) return -3;
        pm_store(&m,target+0x78u+i*4u,pm_load(&m,source+0x78u+i*4u,4),4);
        if (m.error) return m.error;
    }
    pm_store(&m,target+0x10f0u,pm_load(&m,source+0x10f0u,2),2);
    pm_store(&m,target+0x10f2u,pm_load(&m,source+0x10f2u,1),1);
    id=pm_load(&m,target+0x790u,4);pm_store(&m,target+0x110bu,id,4);pm_store(&m,target+0x110fu,id,4);
    pm_store(&m,target+0x14u,target+0x18u,4);pm_store(&m,target+0xde6u,1,1);pm_store(&m,target+0xde5u,1,1);
    for (i=0;i<3;i++) pm_store(&m,target+i*4u,pm_load(&m,source+i*4u,4),4);
    for (i=0;i<3;i++) pm_store(&m,target+0x50u+i*4u,pm_load(&m,source+0x50u+i*4u,4),4);
    for (i=0;i<9;i++) pm_store(&m,target+0x18u+i*4u,pm_load(&m,source+0x18u+i*4u,4),4);
    pm_store(&m,target+0x48u,pm_load(&m,source+0x48u,4),4);pm_store(&m,target+0x10bcu,0,1);
    pm_write32(&m,target+0x66au,pm_i16(&m,source+0x10bau));
    for (at=0x58f584u;at<0x58fbd4u;at+=0x194u)
        if (pm_load(&m,at+4u,4) && pm_load(&m,at,4)==source) pm_store(&m,at,target,4);
    for (at=0x58faa4u;at<0x58fafcu;at+=0x2cu)
        if (pm_load(&m,at-4u,1) && pm_load(&m,at,4)==source) pm_store(&m,at,target,4);
    if (pm_load(&m,target+0xe18u,1)) {
        retire_light(&m,pm_load(&m,target+0xe1au,4));pm_store(&m,target+0xe18u,0,1);
    }
    if (pm_load(&m,target+0xe19u,1)) {
        retire_light(&m,pm_load(&m,target+0xe1eu,4));pm_store(&m,target+0xe19u,0,1);
    }
    id=pm_load(&m,0x53e8b8u,4);head=pm_load(&m,0x54b488u,4);if (m.error) return m.error;
    status=frame_list_remove_value(read,write,ctx,id,head,s->release,s->system_ctx,s->work_limit);if (status) return status;
    id=pm_load(&m,0x53e8b8u,4);if (m.error) return m.error;
    status=frame_entity_queue(read,write,ctx,id,0,s->allocate,s->release,s->system_ctx,s->work_limit);if (status) return status;
    id=pm_load(&m,0x53b69cu,4);head=pm_load(&m,0x54b488u,4);if (m.error) return m.error;
    status=frame_list_append(read,write,ctx,id,head,s->allocate,s->release,s->system_ctx,s->work_limit,&result);if (status) return status;
    pm_store(&m,target+0x10u,pm_load(&m,target+0x10u,4)&0xffffffdfu,4);
    id=pm_load(&m,0x53b69cu,4);descriptor=pm_load(&m,0x545eb8u,4);
    pm_store(&m,0x53e8b8u,id,4);pm_store(&m,0x53e8e0u,id,4);if (m.error) return m.error;
    status=frame_entity_dispatch(read,write,ctx,id,descriptor,0,0,s->method,s->method_ctx,&result);if (status) return status;
    pm_store(&m,0x53e990u,1,4);return m.error;
}
int frame_tank_initialize(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t *out)
{
    static const uint32_t first[]={0x5c,0x78,0x7c,0x80,0x94};
    static const uint32_t rest[]={0x98,0x9c,0xa0,0xa4,0xb4,0xb8,0xbc,0xc0,0xc4,0xc8,0xcc,0xd0,0xd4,0xd8,0xdc,0xe0,0xe4,0xe8,0xec,0xc9e};
    static const uint32_t last[]={0xf0,0xf4,0x764,0x768,0x50,0x54,0x58};
    physics_memory m={read,write,ctx,0};uint32_t o,i,j,p;
    if (!read || !write || !out) return -1;
    o=object_of(&m,entity);pm_store(&m,o+0xde9u,0,1);
    for (i=0;i<sizeof(first)/sizeof(first[0]);i++) pm_store(&m,o+first[i],0,4);
    pm_store(&m,o+0x762u,0,2);
    for (i=0;i<sizeof(rest)/sizeof(rest[0]);i++) pm_store(&m,o+rest[i],0,4);
    for (i=0;i<4u;i++) pm_store(&m,o+0x144u+i*70u,i,4);
    for (j=1;j<3u;j++) for (i=0;i<4u;i++) pm_store(&m,o+0x144u+i*70u+j*4u,0,4);
    for (i=0;i<sizeof(last)/sizeof(last[0]);i++) pm_store(&m,o+last[i],0,4);
    for (i=0;i<12u;i++) pm_store(&m,o+0x18u+i*4u,pm_load(&m,0x532888u+i*4u,4),4);
    pm_store(&m,o+0x14u,o+0x18u,4);
    for (i=0;i<9u;i++) {
        p=o+0x2a6u+i*104u;pm_store(&m,p,0,4);pm_store(&m,p+4u,0,4);pm_store(&m,p+8u,0,4);
        for (j=0;j<12u;j++) pm_store(&m,p+12u+j*4u,pm_load(&m,0x532888u+j*4u,4),4);
    }
    pm_store(&m,o+0x4eu,65520,2);pm_store(&m,o+0xdedu,1,1);pm_store(&m,o+0xcb6u,0,4);pm_store(&m,o+0xcbau,0,4);
    for (i=0;i<3u;i++) for (j=0;j<3u;j++) pm_store(&m,o+0xcc2u+i*7u+j,0,1);
    pm_store(&m,o+0x794u,0,4);pm_store(&m,o+0xacu,0,4);pm_store(&m,o+0xd0au,0,1);pm_store(&m,o+0xd91u,1,4);
    pm_store(&m,o+0xdebu,pm_load(&m,o+0xdeau,1),1);if (m.error) return m.error;
    *out=entity;return 0;
}
int frame_tank_construct(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t parser,const frame_entity_factory_services *e,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t o,parent,i,head,unused;int status;
    if (!read || !write || !e || !e->work_limit || !out) return -1;
    o=object_of(&m,entity);parent=pm_load(&m,0x5466e4u,4);if (m.error) return m.error;
    status=frame_class_dispatch_payload(read,ctx,parent,entity,parser,e->method,e->method_ctx,&unused);if (status) return status;
    if (o) {
        for (i=0;i<3u;i++) pm_store(&m,o+0x78u+i*4u,pm_load(&m,o+i*4u,4),4);
        for (i=0;i<12u;i++) pm_store(&m,o+0x2b2u+i*4u,pm_load(&m,o+0x18u+i*4u,4),4);
        head=pm_load(&m,0x54b488u,4);if (m.error) return m.error;
        status=frame_list_append(read,write,ctx,entity,head,e->allocate,e->release,e->system_ctx,e->work_limit,&unused);if (status) return status;
        head=pm_load(&m,0x54b48cu,4);if (m.error) return m.error;
        status=frame_list_remove_value(read,write,ctx,entity,head,e->release,e->system_ctx,e->work_limit);if (status) return status;
    }
    if (m.error) return m.error;
    *out=0;return 0;
}
int frame_tank_destroy(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *e,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t o,i,shape,bytes,parent,unused;int status;
    static const uint32_t offsets[]={0x366,0x296,0x2fe,0x71e},sizes[]={0x3b2,0x2e2,0x34a,0x75e};
    if (!read || !write || !e || !e->work_limit || !out) return -1;
    o=object_of(&m,entity);
    if (!pm_load(&m,o+0xde6u,1)) for (i=0;i<4u;i++) {
        if ((i==1u || i==2u) && !pm_load(&m,o+0xde5u,1)) continue;
        shape=pm_load(&m,o+offsets[i],4);if (!shape && i<3u) continue;
        bytes=pm_load(&m,o+sizes[i],4);if (m.error) return m.error;
        status=frame_shape_release(read,write,ctx,shape,bytes,e);if (status) return status;
    }
    parent=pm_load(&m,0x5466e4u,4);if (m.error) return m.error;
    status=frame_class_dispatch(read,ctx,parent,entity,e->method,e->method_ctx,&unused);if (status) return status;
    *out=entity;return 0;
}
int frame_player_initialize(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *e,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t o,parent,unused;int status;
    if (!read || !write || !e || !e->work_limit || !out) return -1;
    o=object_of(&m,entity);parent=pm_load(&m,0x545fdcu,4);if (m.error) return m.error;
    status=frame_class_dispatch(read,ctx,parent,entity,e->method,e->method_ctx,&unused);if (status) return status;
    pm_store(&m,o+0x1103u,1,1);pm_store(&m,o+0xde5u,1,1);pm_store(&m,o+0x1105u,0,1);
    pm_store(&m,0x544fa0u,0,4);pm_store(&m,0x544fa8u,0,2);pm_store(&m,0x544fa4u,0,4);pm_store(&m,0x544facu,0,1);pm_store(&m,0x544fb0u,0,1);
    pm_store(&m,o+0x1124u,175,2);if (m.error) return m.error;
    *out=entity;return 0;
}

int frame_player_create(frame_data_read read,frame_data_write write,void *ctx,uint32_t slot,const frame_entity_factory_services *e,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t id,o,storage,i,unused,head;int status;
#define CREATE_TRY(x) do { if (m.error) return m.error;status=(x);if (status) return status; } while (0)
    if (!read || !write || !e || !out || !e->work_limit) return -1;
    CREATE_TRY(frame_entity_create(read,write,ctx,pm_load(&m,0x544f98u,4),e,&id));o=object_of(&m,id);
    pm_store(&m,o+72u,0,4);pm_store(&m,o+0x78u,0,4);pm_store(&m,o+0x7cu,0,4);pm_store(&m,o+0x80u,0,4);pm_store(&m,o+0xdf0u,slot,1);
    if (pm_load(&m,0x52ee9cu,4)==3u) { CREATE_TRY(frame_level_allocate(read,write,ctx,48,&storage));pm_store(&m,o+0x110eu,storage,4);for (i=0;i<12u;i++) pm_store(&m,storage+i*4u,0,4);pm_store(&m,o+0x1112u,UINT32_MAX,4); }
    else pm_store(&m,o+0x110eu,0,4);
    if (pm_load(&m,0x52ee9cu,4)==4u) pm_store(&m,o+0x10c2u,pm_load(&m,0x531ba4u,4),4);
    head=pm_load(&m,0x54b488u,4);CREATE_TRY(frame_list_prepend(read,write,ctx,id,head,e->allocate,e->release,e->system_ctx,e->work_limit,&unused));head=pm_load(&m,0x54b48cu,4);CREATE_TRY(frame_list_remove_value(read,write,ctx,id,head,e->release,e->system_ctx,e->work_limit));CREATE_TRY(frame_entity_activate(read,write,ctx,id,e->method,e->method_ctx,&unused));
    *out=id;return m.error;
#undef CREATE_TRY
}
int frame_player_routes_initialize(frame_data_read read,frame_data_write write,void *ctx,uint32_t limit)
{
    physics_memory m={read,write,ctx,0};uint32_t mode,head,node,id,o,i,checkpoint,used=0;int32_t score;int status;
    if (!read || !write || !limit) return -1;
    mode=pm_load(&m,0x52ee9cu,4);
    if (mode==5u) {
        head=pm_load(&m,0x5468a0u,4);node=pm_load(&m,head,4);
        while (node!=head) { if (m.error) return m.error;if (used++>=limit) return -3;id=pm_load(&m,node+8u,4);o=object_of(&m,id);if (pm_load(&m,o+0x80u,1)) { pm_store(&m,object_of(&m,pm_load(&m,0x53e8b8u,4))+0x790u,id,4);return m.error; }node=pm_load(&m,node,4); }
    }
    for (i=0;i<9u;i++) {
        id=pm_load(&m,0x53e8e0u+i*4u,4);o=object_of(&m,id);if (!id) continue;
        if (m.error) return m.error;
        status=frame_route_nearest(read,ctx,o,limit,&checkpoint);if (status) return status;pm_store(&m,o+0x790u,checkpoint,4);
        if (i>=2u) { pm_store(&m,o+0x110bu,checkpoint,4);pm_store(&m,o+0x110fu,checkpoint,4);status=frame_route_progress(read,ctx,pm_i32(&m,o),pm_i32(&m,o+8u),checkpoint,pm_i16(&m,o+0xddau),limit,&score);if (status) return status;pm_write32(&m,o+0xd18u,score); }
    }
    id=pm_load(&m,0x590410u,4);if (pm_load(&m,0x53e928u,4) && id) { o=object_of(&m,id);if (m.error) return m.error;
        status=frame_route_nearest(read,ctx,o,limit,&checkpoint);if (status) return status;pm_store(&m,o+0x790u,checkpoint,4); }
    return m.error;
}
