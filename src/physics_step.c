#include "physics_step.h"
#include "physics_inactive.h"
#include "physics_vehicle.h"
#include "physics_velocity.h"
#include "physics_drive.h"
#include "physics_pose.h"
#include "physics_body.h"
#include "physics_memory.h"
#include "frame_weapon_spin.h"
#include "frame_ability_boost.h"
#include "frame_scene_effects.h"
#include "frame_environment.h"

static void vector_read(physics_memory *m,uint32_t a,int32_t *v,unsigned n)
{ unsigned i;for (i=0;i<n;++i) v[i]=pm_i32(m,a+i*4u); }
static void vector_write(physics_memory *m,uint32_t a,const int32_t *v,unsigned n)
{ unsigned i;for (i=0;i<n;++i) pm_write32(m,a+i*4u,v[i]); }
static int scene(const physics_vehicle_services *s,int phase,uint32_t o,uint32_t id,int32_t arg)
{ int result;if (!s || !s->scene) return -8;result=s->scene(s->ctx,phase,o,id,arg);return result>0 ? -8 : result; }
static void velocity_transform(physics_memory *m,uint32_t o,int inverse,int large)
{
    int32_t input[3],matrix[9],out[3];vector_read(m,o+(inverse ? 0xe4u : 0x50u),input,3);vector_read(m,o+0x18,matrix,9);
    physics_velocity_transform(input,matrix,out,inverse,large);vector_write(m,o+(inverse ? 0x50u : 0xe4u),out,3);
}
static int attitude(physics_memory *m,uint32_t o,const physics_trig *trig)
{
    physics_attitude_state state;physics_attitude_config config;int status;
    vector_read(m,o+0x18,state.reference,9);vector_read(m,o+0x2b2,state.matrix,9);vector_read(m,o+0x2a6,state.position,3);
    vector_read(m,o+0xb4,state.angular,3);vector_read(m,o+0xd8,state.pivot,3);
    state.longitudinal_delta=pm_i32(m,o+0xcc);state.lateral_delta=pm_i32(m,o+0xc4);
    config.longitudinal_multiplier=pm_load(m,o+0x6f6,2);config.longitudinal_shift=pm_load(m,o+0x6f8,1);
    config.lateral_multiplier=pm_load(m,o+0x6fa,2);config.lateral_shift=pm_load(m,o+0x6fc,1);
    if (m->error) return m->error;
    status=physics_attitude_step(&state,&config,pm_i32(m,0x5904fc),pm_load(m,0x590564,4),trig);if (status) return status;
    vector_write(m,o+0x2b2,state.matrix,9);vector_write(m,o+0x2a6,state.position,3);vector_write(m,o+0xb4,state.angular,3);return m->error;
}
static void velocity_delta(physics_memory *m,uint32_t o,uint32_t offset,const int32_t before[3])
{ unsigned i;for (i=0;i<3;++i) pm_write32(m,o+offset+i*4u,pm_sub(pm_i32(m,o+0x50+i*4u),before[i])); }
static void blend_pair(physics_memory *m,uint32_t o,const int32_t before[3])
{
    unsigned i;for (i=0;i<3;i+=2) pm_write32(m,o+0x50+i*4u,pm_add(before[i],pm_sar(pm_mul(pm_sub(pm_i32(m,o+0x50+i*4u),before[i]),624),10)));
}
int physics_vehicle_step_ambient(physics_world_read read,physics_world_write write,void *ctx,
    uint32_t id,const physics_trig *trig,const physics_vehicle_services *services,const int32_t *inactive_miss_words)
{
    physics_memory memory={read,write,ctx,0},*m=&memory;uint32_t link=pm_load(m,pm_load(m,0x55c694,4)+id*8u,4),o=link ? pm_load(m,link+4,4) : 0;
    int32_t dt=pm_i32(m,0x5904fc),mode,v,timer,before[3];int status,large;unsigned i;uint32_t feedback=0;
    const physics_obstacle_services *obstacle=services ? services->obstacle : 0;
    const physics_collision_services *collision=obstacle ? obstacle->collision : 0;
    if (m->error) return m->error;
    if (!o || dt<=0) return -2;
    if (!pm_load(m,o+0xded,1)) return physics_inactive_step_ambient(read,write,ctx,o,id,trig,services,inactive_miss_words);
    if (pm_load(m,o+0xde5,1)) { status=frame_environment_player_view(read,write,ctx,trig);if (status) return status; }
    pm_write32(m,o+0xd91,pm_neg(pm_i32(m,o+0xd91)));
    if (!pm_load(m,o+0xdea,1)) {
        v=pm_add(pm_i32(m,o+0xa8),pm_sar(pm_mul(pm_sar(pm_i32(m,o+0x90),5),pm_sar(dt,1)),1));
        v=pm_sub(v,pm_mul(dt,256));if (v<0) v=0;
        pm_write32(m,o+0xa8,v);if (v>pm_sub(pm_i32(m,0x531b80),10)) pm_write32(m,o+0xa8,pm_sub(pm_i32(m,0x531b80),10));
    }
    pm_store(m,o+0x250,0,1);pm_store(m,o+0x26d,0,1);pm_write32(m,o+0xc9a,-1);
    timer=pm_i16(m,o+0xdd6);
    if (timer>0) {
        pm_store(m,o+0xdd6,(uint32_t)pm_sub(timer,dt),2);
        v=pm_sub(pm_i32(m,o+0xcaa),pm_mul(pm_i32(m,o+0xcb2),dt));pm_write32(m,o+0xcaa,v<0 ? 0 : v);
        if (pm_i16(m,o+0xdd6)<1 && (!pm_load(m,o+0xde5,1) || pm_load(m,o+0xdcd,1)==8)) {
            pm_write32(m,o+0xc9e,0);pm_store(m,o+0xdd6,0,2);pm_write32(m,o+0xcaa,0);pm_store(m,o+0xdcb,0,1);
        }
    }
    if (pm_i32(m,o+0xdaf)>0) {
        if (m->error) return m->error;
        status=frame_weapon_spin_update(read,write,ctx,o,65536);if (status) return status;
    }
    if (pm_load(m,o+0xdcb,1)) {
        status=frame_ability_boost_tick(read,write,ctx,o,0,0);if (status) return status;
    }
    timer=pm_i16(m,o+0xdd0);
    if (timer) {
        if (dt<timer) pm_store(m,o+0xdd0,(uint32_t)pm_sub(timer,dt),2);
        else {
            pm_store(m,o+0xdd0,0,2);pm_write32(m,o+0x6a6,pm_i32(m,o+0xdd2));
            status=frame_temp_effects_expire(read,write,ctx,(uint32_t)pm_i16(m,o+0xdce));if (status) return status;
        }
    }
    if (m->error) return m->error;
    status=physics_vehicle_contact(read,write,ctx,o,&feedback);if (status) return status;
    if (feedback) { status=scene(services,PHYSICS_SCENE_CONTACT_FEEDBACK,o,id,0);if (status) return status; }
    vector_read(m,o+0x50,before,3);large=physics_velocity_large(before);velocity_transform(m,o,0,large);
    if (m->error) return m->error;
    status=physics_vehicle_drive(read,write,ctx,o,id);if (status) return status;
    if (!pm_load(m,o+0x10bc,1) && pm_i32(m,o+0x10be)<1) { status=physics_yaw_correction(read,write,ctx,o);if (status) return status; }
    velocity_transform(m,o,1,large);pm_write32(m,o+0x54,pm_add(pm_i32(m,o+0x54),pm_i32(m,o+0xc8)));
    if (m->error) return m->error;
    vector_read(m,o+0x50,before,3);
    status=physics_obstacle_collision_step(read,write,ctx,o,id,trig,obstacle);if (status) return status;
    { int32_t impulse[3];for (i=0;i<3;++i) impulse[i]=pm_sub(pm_i32(m,o+0x50+i*4u),before[i]);pm_write32(m,o+0x114,physics_approximate_distance(impulse)); }
    vector_read(m,o+0x50,before,3);
    if (!(pm_load(m,o+0xd90,1)&1u)) {
        mode=pm_i32(m,0x52ee9c);
        if (mode==4 || mode==5) { pm_store(m,0x546014,0,4);pm_write32(m,0x546008,0);pm_write32(m,0x546010,0); }
        if (m->error) return m->error;
        status=physics_tank_collision_step(read,write,ctx,o,collision);if (status) return status;
        if (mode==4 || mode==5) {
            uint32_t peer;blend_pair(m,o,before);peer=pm_load(m,0x546014,4);
            if (peer) { int32_t old[3]={pm_i32(m,0x546008),0,pm_i32(m,0x546010)};blend_pair(m,peer,old); }
        }
    }
    velocity_delta(m,o,0x108,before);if (m->error) return m->error;
    status=physics_deformable_collision_step(read,write,ctx,o,id,trig,obstacle);if (status) return status;
    vector_read(m,o+0x50,before,3);if (m->error) return m->error;
    status=physics_ground_collision_step(read,write,ctx,o,trig,collision);if (status) return status;
    velocity_delta(m,o,0xfc,before);
    for (i=0;i<3;++i) pm_write32(m,o+0x84+i*4u,pm_i32(m,o+0x78+i*4u));
    for (i=0;i<3;++i) pm_write32(m,o+0x78+i*4u,pm_add(pm_i32(m,o+0x78+i*4u),pm_trunc(pm_mul(pm_i32(m,o+0x50+i*4u),dt),14)));
    if (m->error) return m->error;
    status=attitude(m,o,trig);if (status) return status;
    status=physics_body_articulation(read,write,ctx,o);if (status) return status;
    mode=pm_i32(m,0x52ee9c);
    if (pm_load(m,o+0xde5,1) && mode!=2 && mode!=3) { status=physics_player_lean(read,write,ctx,o);if (status) return status; }
    status=physics_pose_update(read,write,ctx,o,trig);if (status) return status;
    status=scene(services,PHYSICS_SCENE_AFTER_POSE,o,id,0);if (status) return status;
    status=physics_contact_hull_refresh(read,write,ctx,o);if (status) return status;
    pm_store(m,o+0xde9,1,1);
    { static const unsigned offsets[4]={0xf8,0xf0,0xc0,0xa0};for (i=0;i<4;++i) pm_write32(m,o+offsets[i],pm_mul(pm_i32(m,o+offsets[i]),40)/dt); }
    return m->error;
}
int physics_vehicle_step(physics_world_read read,physics_world_write write,void *ctx,uint32_t id,const physics_trig *trig,const physics_vehicle_services *services)
{ return physics_vehicle_step_ambient(read,write,ctx,id,trig,services,0); }
