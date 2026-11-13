#include "physics_vehicle.h"
#include "physics_sampling.h"
#include "physics_memory.h"

static void read_vector(physics_memory *m,uint32_t a,int32_t *v,unsigned n)
{ unsigned i;for (i=0;i<n;++i) v[i]=pm_i32(m,a+i*4u); }
static void write_vector(physics_memory *m,uint32_t a,const int32_t *v,unsigned n)
{ unsigned i;for (i=0;i<n;++i) pm_write32(m,a+i*4u,v[i]); }
#define SUSPENSION_FIELDS(F) \
    F(angular0,0xb4) F(angular2,0xbc) F(tilt0,0xd0) F(tilt1,0xd4) \
    F(penetrating_input_sum,0xf0) F(travel_sum,0xf8) F(vertical_delta,0xc8) \
    F(world_vy,0x54) F(position_y,0x7c) F(timer,0xd14)
#define CONTACT_CONFIG_FIELDS(F) \
    F(lower,0x64a) F(upper,0x64e) F(force_positive,0x6d2) F(force_negative,0x6d6) \
    F(output_gain,0x6da) F(tilt_gain,0x6de) F(shape_shift,0x6e2) F(restore_positive,0x6e6) \
    F(restore_negative,0x6ea) F(rate_damping,0x6ee) F(travel_dead_step,0x6f2)
int physics_vehicle_contact(physics_world_read read,physics_world_write write,void *ctx,uint32_t o,uint32_t *feedback_required)
{
    physics_memory memory={read,write,ctx,0},*m=&memory;physics_sampling_state s={0};physics_sampling_environment e={0};
    physics_suspension_state suspension={0};physics_suspension_environment se={0};physics_contact_config config;
    int32_t layout[5],pivot[3];unsigned i;uint32_t handle;int status;
    static const uint32_t layout_offsets[5]={0x632,0x636,0x63a,0x63e,0x646};
    for (i=0;i<4;++i) {
        uint32_t a=o+0x11c+i*70u;physics_primary_point *p=&s.primary.point[i];physics_suspension_point *q=&suspension.point[i];
        read_vector(m,a,p->local,3);read_vector(m,a+12,p->world,3);p->kind=pm_i16(m,a+24);
        p->gap=pm_i32(m,a+28);p->previous_gap=pm_i32(m,a+32);p->raw_height=pm_i32(m,a+36);p->travel=pm_i32(m,a+44);p->flags=pm_load(m,a+68,2);read_vector(m,a+56,p->normal,3);
        q->kind=p->kind;q->gap=p->gap;q->corner=pm_i32(m,a+40);q->travel=p->travel;q->rate=pm_i32(m,a+48);q->output=pm_i32(m,a+52);
    }
#define LOAD_SUSPENSION(field,offset) suspension.field=pm_i32(m,o+(offset));
    SUSPENSION_FIELDS(LOAD_SUSPENSION)
#undef LOAD_SUSPENSION
#define LOAD_CONFIG(field,offset) config.field=pm_i32(m,o+(offset));
    CONTACT_CONFIG_FIELDS(LOAD_CONFIG)
#undef LOAD_CONFIG
    s.primary.load=pm_i32(m,o+0xf4);s.primary.airborne_count=(int32_t)pm_load(m,o+0xde1,1);s.primary.special_count=(int32_t)pm_load(m,o+0xde2,1);s.primary.flagged_count=(int32_t)pm_load(m,o+0xde3,1);s.primary.has_extra=(int32_t)pm_load(m,o+0xdee,1);read_vector(m,o+0x26e,s.primary.extra,4);
    s.effect.timer=pm_i16(m,o+0xe0b);s.effect.restore=pm_i16(m,o+0xe0d);s.effect.active=(int32_t)pm_load(m,o+0xe08,1);s.effect.phase=pm_i32(m,o+0xe0f);s.effect.impulse0=pm_i32(m,o+0x768);s.effect.impulse2=pm_i32(m,o+0x770);s.effect.pose=pm_i32(m,o+0x30a);s.effect.seed=pm_load(m,0x55b6ac,4);
    for (i=0;i<2;++i) {
        uint32_t a=o+0x3b6+i*104u;read_vector(m,a+0x34,e.primary.track[i].matrix,9);read_vector(m,a+0x28,e.primary.track[i].origin,3);
        read_vector(m,o+0x234+i*29u,e.auxiliary_local[i],3);read_vector(m,o+0x240+i*29u,s.auxiliary[i].world,3);
        s.auxiliary[i].kind=pm_i32(m,o+0x24c+i*29u);s.auxiliary[i].cached=(int32_t)pm_load(m,o+0x250+i*29u,1);
    }
    read_vector(m,o+0x716,s.materials,2);e.primary.root=pm_load(m,0x55ae60,4);e.primary.surface_table=0x54603c;
    for (i=0;i<16;++i) e.primary.noise[i]=(uint16_t)pm_load(m,UINT32_C(0x546748)+i*2u,2);
    e.player=pm_load(m,o+0xde5,1);e.dt=pm_i32(m,0x5904fc);e.frame=pm_load(m,0x590564,4);e.primary.force_surface=pm_load(m,0x53e414,4) && e.player;
    e.primary.effect_multiplier=pm_load(m,0x531b9c,2);e.primary.effect_shift=pm_load(m,0x531b9e,1);
    e.primary.load_base=pm_i32(m,o+0x69a);e.primary.load_floor=pm_i32(m,o+0x69e);e.primary.load_multiplier=pm_load(m,o+0x6a2,2);e.primary.load_shift=pm_load(m,o+0x6a4,1);
    handle=pm_load(m,o+0xd8c,4);
    if (handle) { uint32_t link=pm_load(m,pm_load(m,0x55c694,4)+handle*8u,4);if (link) e.primary.deformable=pm_load(m,link+4,4); }
    se.dt=e.dt;se.environment_flag=pm_i32(m,0x546000);se.session_active=pm_i32(m,0x58e7ac);se.mode=pm_i32(m,0x52ed90);se.is_player=(int32_t)e.player;
    se.base_x=pm_i32(m,o);se.base_z=pm_i32(m,o+8);se.region_x0=pm_i32(m,0x531be8);se.region_z0=pm_i32(m,0x531bf0);se.region_x1=pm_i32(m,0x531bf8);se.region_z1=pm_i32(m,0x531c00);
    se.player_multiplier=pm_load(m,0x531948,4);se.player_shift=pm_load(m,0x53194c,1);se.other_multiplier=pm_load(m,0x531950,4);se.other_shift=pm_load(m,0x531954,1);se.penetration_shift=pm_load(m,o+0x700,1);
    for (i=0;i<5;++i) layout[i]=pm_i32(m,o+layout_offsets[i]);
    if (m->error) return m->error;
    status=physics_contact_pipeline(read,write,ctx,&s,&suspension,pivot,&e,&config,&se,layout);if (status<0) return status;
    for (i=0;i<4;++i) {
        uint32_t a=o+0x11c+i*70u;physics_primary_point *p=&s.primary.point[i];physics_suspension_point *q=&suspension.point[i];
        write_vector(m,a+12,p->world,3);pm_store(m,a+24,(uint32_t)p->kind,2);pm_write32(m,a+28,p->gap);pm_write32(m,a+32,p->previous_gap);pm_write32(m,a+36,p->raw_height);
        pm_write32(m,a+44,q->travel);pm_write32(m,a+48,q->rate);pm_write32(m,a+52,q->output);write_vector(m,a+56,p->normal,3);pm_store(m,a+68,p->flags,2);
    }
#define STORE_SUSPENSION(field,offset) pm_write32(m,o+(offset),suspension.field);
    SUSPENSION_FIELDS(STORE_SUSPENSION)
#undef STORE_SUSPENSION
    pm_write32(m,o+0xf4,s.primary.load);pm_store(m,o+0xde1,(uint32_t)s.primary.airborne_count,1);pm_store(m,o+0xde2,(uint32_t)s.primary.special_count,1);pm_store(m,o+0xde3,(uint32_t)s.primary.flagged_count,1);pm_store(m,o+0xdee,(uint32_t)s.primary.has_extra,1);write_vector(m,o+0x26e,s.primary.extra,4);
    pm_store(m,o+0xe0b,(uint32_t)s.effect.timer,2);pm_store(m,o+0xe08,(uint32_t)s.effect.active,1);pm_write32(m,o+0xe0f,s.effect.phase);pm_write32(m,o+0x768,s.effect.impulse0);pm_write32(m,o+0x770,s.effect.impulse2);pm_write32(m,o+0x30a,s.effect.pose);pm_store(m,0x55b6ac,s.effect.seed,4);
    for (i=0;i<2;++i) { write_vector(m,o+0x240+i*29u,s.auxiliary[i].world,3);pm_write32(m,o+0x24c+i*29u,s.auxiliary[i].kind);pm_store(m,o+0x250+i*29u,(uint32_t)s.auxiliary[i].cached,1); }
    write_vector(m,o+0x716,s.materials,2);write_vector(m,o+0xd8,pivot,3);if (feedback_required) *feedback_required=(uint32_t)s.feedback_required;
    return m->error;
}
