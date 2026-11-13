#include "frame_player_state.h"
#include "frame_player_input.h"
#include "frame_progress.h"
#include "frame_controls.h"
#include "physics_memory.h"

int frame_player_mode_timer(frame_data_read read,frame_data_write write,void *ctx,uint32_t object)
{
    physics_memory mem={read,write,ctx,0},*m=&mem;
    if (!read || !write) return -1;
    if (pm_load(m,0x52ee9cu,4)==4)
        pm_write32(m,object+0x10beu,pm_sub(pm_i32(m,object+0x10beu),pm_i32(m,0x5904fcu)));
    return m->error;
}

int frame_player_fire(frame_data_read read,frame_data_write write,void *ctx,uint32_t object,uint32_t entity,
                        frame_native_method method,void *method_ctx)
{
    physics_memory m={read,write,ctx,0};uint32_t down=0,result,code;int status;
    if (!read || !write) return -1;
    if (!pm_load(&m,object+0xdf0u,1)) {
        code=pm_load(&m,0x592030u,4);if (m.error) return m.error;
        status=frame_control_key(read,ctx,code,&result);if (status) return status;down=result==1;
        if (!down && pm_load(&m,0x53ccdcu,4)) {
            code=pm_load(&m,0x592930u,4);
            if (code) { status=frame_control_key(read,ctx,code,&result);if (status) return status;down=result==1; }
        }
    }
    if (m.error) return m.error;
    if (down) {
        uint32_t descriptor=pm_load(&m,0x545edcu,4);if (m.error) return m.error;
        status=frame_entity_dispatch(read,write,ctx,entity,descriptor,0,0,method,method_ctx,&result);
        if (status) return status;
    }
    return frame_player_mode_timer(read,write,ctx,object);
}

int frame_player_aux_brake(frame_data_read read,frame_data_write write,void *ctx,
                            uint32_t object,uint32_t auxiliary_down)
{
    physics_memory mem={read,write,ctx,0},*m=&mem;
    int32_t mode;
    uint32_t saved;
    if (!read || !write) return -1;
    if (!pm_load(m,object+0xdd0u,2)) {
        if (auxiliary_down && pm_abs(pm_i32(m,object+0xecu))>250000) {
            pm_store(m,object+0x10b6u,(uint32_t)pm_sub(pm_i16(m,object+0x10b6u),pm_mul(pm_i32(m,0x5904fcu),2)),2);
            if (pm_i16(m,object+0x10b6u)<0) pm_store(m,object+0x10b6u,0,2);
            mode=pm_i32(m,0x52ee9cu);
            pm_store(m,object+0x10b8u,mode==4 ? 400u : mode==5 ? 200u : 0u,2);
            if (!pm_load(m,object+0x10bcu,1)) {
                saved=pm_load(m,object+0x66au,2);
                pm_write32(m,object+0x66au,310);
                pm_store(m,object+0x10bau,saved,2);pm_store(m,object+0x10bcu,1,1);
            }
        } else if (pm_load(m,object+0x10bcu,1) && !m->error) {
            m->error=frame_player_restore_overrides(read,write,ctx,object);
        }
        if (auxiliary_down) {
            pm_write32(m,object+0x98u,40000);pm_write32(m,object+0x94u,0);pm_store(m,object+0x10bdu,1,1);
        } else pm_store(m,object+0x10bdu,0,1);
    } else if (pm_load(m,object+0x10bcu,1)) {
        uint32_t target=pm_load(m,0x546020u+pm_load(m,object+0x716u,4)*40u,2);
        pm_store(m,object+0x10b8u,pm_load(m,object+0x6c2u,2),2);
        saved=(uint32_t)pm_i16(m,object+0x10bau);
        pm_store(m,object+0x10b6u,target,2);pm_store(m,object+0x66au,saved,4);pm_store(m,object+0x10bcu,0,1);
    }
    return m->error;
}

static uint32_t entity_object(physics_memory *m,uint32_t id)
{
    uint32_t slot=pm_load(m,pm_load(m,0x55c694u,4)+id*8u,4);
    return slot ? pm_load(m,slot+4u,4) : 0;
}
int frame_player_route_update(frame_data_read read,frame_data_write write,void *ctx,
                                uint32_t object,uint32_t work_limit)
{
    physics_memory mem={read,write,ctx,0},*m=&mem;
    int32_t mode,score,relative,limit;
    uint32_t checkpoint,target;
    int status;
    if (!read || !write || !work_limit) return -1;
    mode=pm_i32(m,0x52ee9cu);
    if (mode!=2 && mode!=4) {
        int32_t x=pm_i32(m,object),z=pm_i32(m,object+8u),lap=pm_i16(m,object+0xddau);
        checkpoint=pm_load(m,object+0x790u,4);
        if (m->error) return m->error;
        status=frame_route_progress(read,ctx,x,z,checkpoint,lap,work_limit,&score);
        if (status) return status;
        checkpoint=pm_load(m,object+0x790u,4);
        pm_write32(m,object+0xd18u,score);
        target=entity_object(m,checkpoint);
        relative=pm_sub(score,pm_mul(pm_i16(m,object+0xddau),10240000));
        limit=pm_i32(m,0x546894u);
        if (pm_i32(m,target+0x90u)>limit && relative<pm_sub(pm_mul(limit,204800),204800) && relative>0)
            pm_store(m,object+0x790u,pm_load(m,0x546898u,4),4);
    }
    return m->error;
}
int frame_player_progress_state(frame_data_read read,frame_data_write write,void *ctx,
                                 uint32_t object,uint32_t entity)
{
    physics_memory mem={read,write,ctx,0},*m=&mem;
    int32_t current,previous,timer,dt;
    uint32_t state,rank,i,best_object=0;
    int32_t best=0,gap,denominator,boost,accel;
    if (!read || !write) return -1;
    if (pm_load(m,object+0xdeau,1) && !((entity^pm_load(m,0x590564u,4))&7u)) {
        state=pm_load(m,object+0x1105u,1);
        current=pm_i32(m,object+0xd18u);previous=pm_i32(m,object+0x110au);
        if (!state) {
            if (current<previous) { pm_store(m,object+0x1105u,1,1);pm_store(m,object+0x1108u,175,2); }
        } else if (state==1) {
            if (current<previous) {
                timer=pm_i16(m,object+0x1108u);dt=pm_i32(m,0x5904fcu);
                if (timer<=dt) { pm_store(m,object+0x1105u,2,1);pm_store(m,object+0x1108u,50,2); }
                else pm_store(m,object+0x1108u,(uint32_t)pm_sub(timer,dt),2);
            } else pm_store(m,object+0x1105u,0,1);
        } else if (state==2) {
            timer=pm_i16(m,object+0x1108u);dt=pm_i32(m,0x5904fcu);
            if (timer>dt) pm_store(m,object+0x1108u,(uint32_t)pm_sub(timer,dt),2);
            else if (current<previous) pm_store(m,object+0x1108u,50,2);
            else pm_store(m,object+0x1105u,0,1);
        }
    }
    current=pm_i32(m,object+0xd18u);
    pm_write32(m,object+0x110au,current);
    if (pm_load(m,0x530c50u,4) && pm_load(m,0x52ee9cu,4)==5) {
        rank=pm_load(m,object+0xddcu,1);
        if (rank==1) {
            pm_write32(m,object+0x68eu,pm_i32(m,object+0x1116u));
            pm_write32(m,object+0x66eu,pm_i32(m,object+0x111au));
        } else {
            for (i=0;i<7;i++) {
                uint32_t id=pm_load(m,0x53e8e8u+i*4u,4),other=0;
                if (id) other=entity_object(m,id);
                if (other && pm_i32(m,other+0xd18u)>best) {
                    best=pm_i32(m,other+0xd18u);best_object=other;
                }
            }
            if (best_object) {
                gap=pm_sub(pm_i32(m,best_object+0xd18u),current);denominator=7-(int32_t)rank;
                if (m->error) return m->error;
                if (!denominator || (gap==INT32_MIN && denominator==-1)) return -2;
                gap/=denominator;
                if (gap>0) {
                    accel=pm_sar(gap,6);boost=pm_mul(gap,16);
                    dt=pm_i32(m,0x5904fcu);
                    if (accel>(dt<71 ? 500 : 50)) accel=dt<71 ? 500 : 50;
                    if (boost>900000) boost=900000;
                    pm_write32(m,object+0x68eu,pm_add(pm_i32(m,object+0x1116u),accel));
                    pm_write32(m,object+0x66eu,pm_add(pm_i32(m,object+0x111au),boost));
                }
            }
        }
    }
    return m->error;
}
