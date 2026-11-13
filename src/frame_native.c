#include "frame_native.h"
#include "frame_camera.h"
#include "frame_spatial.h"
#include "frame_deformable.h"
#include "frame_ability_boost.h"
#include "frame_weapon_spin.h"
#include "frame_fall.h"
#include "frame_bounce.h"
#include "frame_closer.h"
#include "frame_powerup.h"
#include "frame_mine.h"
#include "frame_explosion.h"
#include "frame_projectile.h"
#include "frame_ufo.h"
#include "frame_player.h"
#include "frame_train.h"
#include "frame_flock.h"
#include "frame_lamps.h"
#include "frame_mover.h"
#include "frame_spin.h"
#include "frame_player_lifecycle.h"
#include "frame_ai.h"
#include "frame_tank_model.h"
#include "frame_tank_hit.h"
#include "frame_vehicle_scene.h"
#include "physics_step.h"
#include "physics_effects.h"
#include "physics_entities.h"
#include "physics_pose.h"
#include "physics_memory.h"

static const uint32_t methods[]={
    0x401bd0,0x403b70,0x403d60,0x403d90,0x403e50,
    0x407000,0x437be0,0x504e50,0x504eb0,0x503cf0,0x503d30,0x503d80,0x503ea0,0x504c40,0x504c50,0x504000,0x504010,0x437bc0,0x437bf0,
    0x4079a0,0x407760,0x408170,0x407f70,0x407790,0x4077e0,0x407f00,0x4079f0,0x41e890,0x41f6e0,
    0x41e9a0,0x410730,0x43f5d0,0x41cf80,0x41c980,0x41dc00,0x41d460,0x406280,0x43d250,0x504020,
    0x41c230,0x41c270,0x41c300,0x41c7a0,0x41c0a0,0x41c190,0x41e960,0x40ab90,0x422b50,0x43f6a0,0x4298e0,0x40aff0,
    0x43f670,0x504250,0x41f720,0x4041e0,0x4041f0,0x503e40,
    0x42a440,0x42a060,0x429fc0,0x41b740,0x41b870,0x442390,0x403a10,
    0x40a3e0,0x40a5f0,0x40a9d0,0x40ac30,
    0x437930,0x438cf0,0x437b30,0x421720,0x4f40e0,0x4350a0,0x437b00,0x421790,0x4218d0,
    0x437c70,0x438970,0x438980,0x4302c0,0x430560,0x4305b0,0x4302f0,0x430a30,
    0x405170,0x4051c0,0x404c30,0x404e00,0x4305a0,0x406390,0x4063e0,0x406460,0x41c620,0x41c660,0x4153c0,
    0x422c30,0x422d00,0x422b10,0x423220,0x422050,0x4227d0,0x421e60,
    0x4151e0,0x415220,0x415230,0x415290,0x414e40,0x4089e0,0x415050,0x429790,0x42f8c0,0x421c10,0x438440,0x440bf0,0x440cc0,0x440b80,0x428680,0x43c700,0x41f930,0x404350,0x404720,0x43f370,0x43e050,0x40b180,0x43e360,0x421bc0,0x40b250,0x40b530,0x40b160,0x40b570,0x43d2a0,0x4057c0,0x406290,0x413520,0x413890,0x413a10,0x404040,0x403f90,0x415930,0x415980,0x415510,0x4153d0,0x42b4f0,0x42b620,0x42ac90,0x42aeb0,0x42bcb0,0x4057f0
};
uint32_t frame_native_method_count(void) { return (uint32_t)(sizeof(methods)/sizeof(methods[0])); }
int frame_native_method_at(uint32_t index,uint32_t *target)
{ if (!target || index>=frame_native_method_count()) return -1;*target=methods[index];return 0; }
int frame_native_has_method(uint32_t target)
{ uint32_t i;for (i=0;i<frame_native_method_count();i++) if (methods[i]==target) return 1;return 0; }
int frame_native_bind(frame_native_context *c)
{
    frame_entity_native_context *e;
    if (!c) return -1;
    e=&c->base.base.base.base;if (!e->read || !e->write || !e->services.work_limit) return -1;
    e->services.method=frame_native_dispatch;e->services.method_ctx=c;
    c->base.base.shatter.pair_method=frame_native_pair;c->base.base.shatter.pair_ctx=c;
    c->unknown_target=0;c->unknown_entity=0;return 0;
}
int frame_native_pair(void *ctx,uint32_t target,uint32_t entity,uint32_t payload,uint32_t second,uint32_t *out)
{
    frame_native_context *c=(frame_native_context *)ctx;
    if (!c || !out) return -1;
    if (target==0x403de0u) { frame_entity_native_context *e=&c->base.base.base.base;int status=frame_ai_hit_pair(e->read,e->write,e->data_ctx,entity,payload,second,frame_native_pair,c);if (!status) *out=0;return status; }
    if (target!=0x438540u) { c->unknown_target=target;c->unknown_entity=entity;return -5; }
    return frame_shatter_pair_native(&c->base.base,target,entity,payload,second,out);
}
static int physical_method(void *ctx,uint32_t target,uint32_t entity,uint32_t payload,uint32_t *out)
{ return frame_native_dispatch(ctx,target,entity,1,payload,out); }
static int physical_feedback(void *ctx,int32_t intensity)
{
    frame_native_context *c=(frame_native_context *)ctx;frame_entity_native_context *e=&c->base.base.base.base;
    return frame_feedback_impact(e->read,e->write,e->data_ctx,(uint32_t)intensity,c->base.base.shatter.feedback,c->base.base.shatter.feedback_ctx);
}
static int physical_scene(void *ctx,int phase,uint32_t object,uint32_t entity,int32_t argument)
{
    frame_native_context *c=(frame_native_context *)ctx;frame_entity_native_context *e=&c->base.base.base.base;uint32_t unused;(void)object;
    switch (phase) {
    case PHYSICS_SCENE_CONTACT_FEEDBACK:return frame_feedback_surface(e->read,e->write,e->data_ctx,c->base.base.shatter.feedback,c->base.base.shatter.feedback_ctx);
    case PHYSICS_SCENE_AFTER_POSE:return frame_vehicle_scene_active(e->read,e->write,e->data_ctx,object,entity,c->base.base.base.parameters.trig);
    case PHYSICS_SCENE_INACTIVE_EFFECTS:return frame_vehicle_scene_inactive(e->read,e->write,e->data_ctx,object,c->base.base.base.parameters.trig);
    case PHYSICS_SCENE_OBJECT_SOUND:return 0; /* explicitly disabled audio device */
    case PHYSICS_SCENE_ENTITY_ACTIVATE:return frame_entity_activate(e->read,e->write,e->data_ctx,entity,e->services.method,e->services.method_ctx,&unused);
    case PHYSICS_SCENE_REMOVE_GRID:return frame_spatial_remove(e->read,e->write,e->data_ctx,entity,(uint32_t)argument,e->services.release,e->services.system_ctx,e->services.work_limit);
    case PHYSICS_SCENE_FORCE_FEEDBACK:return frame_feedback_impact(e->read,e->write,e->data_ctx,(uint32_t)argument,c->base.base.shatter.feedback,c->base.base.shatter.feedback_ctx);
    default:return -5;
    }
}
int frame_native_dispatch(void *ctx,uint32_t target,uint32_t entity,uint32_t has_payload,uint32_t payload,uint32_t *out)
{
    frame_native_context *c=(frame_native_context *)ctx;frame_entity_native_context *e;int status;
    if (!c || !out) return -1;
    e=&c->base.base.base.base;
    if (!frame_native_has_method(target)) { c->unknown_target=target;c->unknown_entity=entity;return -5; }
    switch (target) {
    case 0x401bd0: {
        frame_ai_services services={&e->services,c->base.base.base.parameters.trig,c->base.base.shatter.feedback,c->base.base.shatter.feedback_ctx};
        status=frame_ai_tick(e->read,e->write,e->data_ctx,entity,&services);break;
    }
    case 0x403b70:status=frame_ai_construct(e->read,e->write,e->data_ctx,entity,payload,&e->services,c->base.base.base.parameters.shapes);break;
    case 0x403d60:return frame_ai_parent(e->read,e->data_ctx,entity,&e->services,out);
    case 0x403d90:case 0x403e50:status=frame_ai_hit(e->read,e->write,e->data_ctx,entity,payload,target==0x403d90u,&e->services);break;
    case 0x403a10:return frame_player_finish_owner_initialize(e->read,e->write,e->data_ctx,entity,&e->services,out);
    case 0x4057f0:status=frame_camera_chase_tick(e->read,e->write,e->data_ctx,entity,payload,&e->services,c->base.base.base.parameters.trig,c->camera_matrix_tail);break;
    case 0x42bcb0:status=frame_camera_start_tick(e->read,e->write,e->data_ctx,entity,payload,&e->services,c->base.base.base.parameters.trig);break;
    case 0x42b4f0:status=frame_spin_initialize(e->read,e->write,e->data_ctx,entity,&e->services);break;
    case 0x42b620:status=frame_spin_construct(e->read,e->write,e->data_ctx,entity,payload,&e->services);break;
    case 0x42ac90:status=frame_spin_hit(e->read,e->write,e->data_ctx,entity,payload,&e->services,c->base.base.base.parameters.trig,c->base.base.shatter.feedback,c->base.base.shatter.feedback_ctx);break;
    case 0x42aeb0:status=frame_spin_tick(e->read,e->write,e->data_ctx,entity,&e->services,c->base.base.base.parameters.trig);break;
    case 0x415930:status=frame_mover_initialize(e->read,e->write,e->data_ctx,entity,&e->services);break;
    case 0x415980:status=frame_mover_construct(e->read,e->write,e->data_ctx,entity,payload,&e->services);break;
    case 0x415510:status=frame_mover_tick(e->read,e->write,e->data_ctx,entity,&e->services,c->base.base.base.parameters.trig);break;
    case 0x4153d0:status=frame_mover_hit(e->read,e->write,e->data_ctx,entity,payload,&e->services,&c->explosion,c->base.base.shatter.feedback,c->base.base.shatter.feedback_ctx);break;
    case 0x404040:status=frame_background_construct(e->read,e->write,e->data_ctx,entity,payload,&e->services);break;
    case 0x403f90:status=frame_background_tick(e->read,e->write,e->data_ctx,entity,&e->services);break;
    case 0x413520:status=frame_lamps_construct(e->read,e->write,e->data_ctx,entity,payload,&e->services,&c->base.base.base.parameters);if (!status) *out=entity;return status;
    case 0x413890:status=frame_lamps_tick(e->read,e->write,e->data_ctx,entity,e->services.work_limit);if (!status) *out=entity;return status;
    case 0x413a10:status=frame_lamps_render(e->read,e->write,e->data_ctx,entity,e->services.work_limit,&c->actors);if (!status) *out=entity;return status;
    case 0x4079f0:return frame_deformable_construct_resources(e->read,e->write,e->data_ctx,entity,payload,&e->services,c->base.base.base.parameters.shapes,out);
    case 0x43d2a0:case 0x4057c0:case 0x406290:status=frame_camera_parameters(e->read,e->write,e->data_ctx,target,entity,payload,&e->services);break;
    case 0x40b250:status=frame_flock_construct(e->read,e->write,e->data_ctx,entity,payload,&e->services,&c->base.base.base.parameters);break;
    case 0x40b530:status=frame_flock_tick(e->read,e->write,e->data_ctx,entity,&e->services,c->base.base.base.parameters.trig);break;
    case 0x40b160:status=frame_flock_start(e->read,e->write,e->data_ctx,entity,&e->services);break;
    case 0x40b570:status=frame_flock_destroy(e->read,e->write,e->data_ctx,entity,&e->services);if (!status) *out=entity;return status;
    case 0x421bc0:status=frame_ufo_player_request(e->read,e->write,e->data_ctx,entity,&e->services);break;
    case 0x43e360:status=frame_train_construct(e->read,e->write,e->data_ctx,entity,payload,&e->services,&c->base.base.base.parameters);break;
    case 0x43f370:status=frame_train_tick(e->read,e->write,e->data_ctx,entity,&e->services,c->base.base.base.parameters.trig);break;
    case 0x43e050:status=frame_train_start(e->read,e->write,e->data_ctx,entity,&e->services);break;
    case 0x40b180:status=frame_train_stop(e->read,e->write,e->data_ctx,entity,&e->services);break;
    case 0x404350:status=frame_projectile_emitter_tick(e->read,e->write,e->data_ctx,entity,&e->services,c->base.base.base.parameters.trig);break;
    case 0x404720:status=frame_projectile_emitter_construct(e->read,e->write,e->data_ctx,entity,payload,&e->services,&c->base.base.base.parameters);break;
    case 0x41f930: {
        frame_player_lap_services lap={0};
        if (c->player_network) lap=*c->player_network;
        lap.method=e->services.method;lap.method_ctx=e->services.method_ctx;lap.allocate=e->services.allocate;lap.release=e->services.release;lap.system_ctx=e->services.system_ctx;lap.work_limit=e->services.work_limit;
        status=frame_player_tick(e->read,e->write,e->data_ctx,entity,&e->services,c->base.base.base.parameters.trig,&lap,&c->explosion,c->player_death_contact_words);
        if (!status) *out=entity;
        return status;
    }
    case 0x43c700:status=frame_camera_track_tick(e->read,e->write,e->data_ctx,entity,payload,&e->services,c->base.base.base.parameters.trig,c->camera_matrix_tail);break;
    case 0x428680:status=frame_projectile_tick(e->read,e->write,e->data_ctx,entity,&e->services,c->base.base.base.parameters.trig,&c->base.base.shatter,&c->explosion);break;
    case 0x440bf0:status=frame_ufo_construct(e->read,e->write,e->data_ctx,entity,&e->services);break;
    case 0x440cc0:status=frame_ufo_stop(e->read,e->write,e->data_ctx,entity,&e->services);break;
    case 0x440b80:status=frame_ufo_tick(e->read,e->write,e->data_ctx,entity,&e->services,c->base.base.base.parameters.trig);break;
    case 0x438440:status=frame_tank_shell_hit(e->read,e->write,e->data_ctx,entity,payload,c->base.base.shatter.feedback,c->base.base.shatter.feedback_ctx);break;
    case 0x42f8c0:return frame_projectile_tank_fire(e->read,e->write,e->data_ctx,entity,&e->services,c->base.base.base.parameters.trig,c->base.base.shatter.feedback,c->base.base.shatter.feedback_ctx,out);
    case 0x421c10:return frame_projectile_player_fire(e->read,e->write,e->data_ctx,entity,&e->services,out);
    case 0x429790:status=frame_projectile_stop(e->read,e->write,e->data_ctx,entity,&e->services);break;
    case 0x415050:status=frame_mine_contact(e->read,e->write,e->data_ctx,entity,payload,&e->services,&c->explosion,c->base.base.shatter.feedback,c->base.base.shatter.feedback_ctx);break;
    case 0x4089e0:status=frame_explosion_tick(e->read,e->write,e->data_ctx,entity,&e->services);break;
    case 0x4151e0:status=frame_mine_construct(e->read,e->write,e->data_ctx,entity,payload,&e->services,c->base.base.base.parameters.trig);break;
    case 0x415220:status=frame_mine_parent(e->read,e->write,e->data_ctx,entity,&e->services);break;
    case 0x415230:status=frame_mine_remove(e->read,e->write,e->data_ctx,entity,&e->services);break;
    case 0x415290:status=frame_mine_mark_inactive(e->read,e->write,e->data_ctx,entity);break;
    case 0x414e40:status=frame_mine_tick(e->read,e->write,e->data_ctx,entity,&e->services,c->base.base.base.parameters.trig,c->base.base.shatter.feedback,c->base.base.shatter.feedback_ctx);break;
    case 0x4227d0:return frame_powerup_construct(e->read,e->write,e->data_ctx,entity,payload,&e->services,c->base.base.base.parameters.trig,out);
    case 0x421e60:status=frame_powerup_collect(e->read,e->write,e->data_ctx,entity,payload,&e->services,c->base.base.shatter.feedback,c->base.base.shatter.feedback_ctx);break;
    case 0x422050:status=frame_powerup_tick(e->read,e->write,e->data_ctx,entity,&e->services,c->base.base.base.parameters.trig);break;
    case 0x422c30:status=frame_powerup_capture(e->read,e->write,e->data_ctx,entity,&e->services);break;
    case 0x422d00:status=frame_powerup_restore(e->read,e->write,e->data_ctx,entity,&e->services);break;
    case 0x422b10:status=frame_powerup_remove(e->read,e->write,e->data_ctx,entity,&e->services);break;
    case 0x423220:status=frame_powerup_disable(e->read,e->write,e->data_ctx,entity,&e->services);break;
    case 0x406390:return frame_closer_construct(e->read,e->write,e->data_ctx,entity,payload,&e->services,out);
    case 0x4063e0:status=frame_closer_hit(e->read,e->write,e->data_ctx,entity,payload,&e->services,c->base.base.shatter.feedback,c->base.base.shatter.feedback_ctx);break;
    case 0x406460:status=frame_closer_tick(e->read,e->write,e->data_ctx,entity,&e->services,c->base.base.base.parameters.trig);break;
    case 0x41c620:return frame_object_move(e->read,e->write,e->data_ctx,entity,payload,&e->services,out);
    case 0x41c660:status=frame_object_collidable(e->read,e->write,e->data_ctx,entity,payload,&e->services);break;
    case 0x4153c0: {
        physics_memory m={e->read,e->write,e->data_ctx,0};pm_store(&m,0x55b680u,pm_load(&m,0x55ae48u,4),4);status=m.error;break;
    }
    case 0x405170:status=frame_bounce_initialize(e->read,e->write,e->data_ctx,entity,&e->services);break;
    case 0x4051c0:return frame_bounce_construct(e->read,e->write,e->data_ctx,entity,payload,&e->services,out);
    case 0x4305a0:
        if (payload==1u) { uint32_t unused;status=frame_entity_activate(e->read,e->write,e->data_ctx,entity,e->services.method,e->services.method_ctx,&unused); }
        else status=frame_entity_queue(e->read,e->write,e->data_ctx,entity,0,e->services.allocate,e->services.release,e->services.system_ctx,e->services.work_limit);
        break;
    case 0x404c30:status=frame_bounce_hit(e->read,e->write,e->data_ctx,entity,payload,&e->services,c->base.base.base.parameters.trig,c->base.base.shatter.feedback,c->base.base.shatter.feedback_ctx);break;
    case 0x404e00:status=frame_bounce_tick(e->read,e->write,e->data_ctx,entity,&e->services,c->base.base.base.parameters.trig);break;
    case 0x4f40e0:*out=0;return 0; /* original is exactly RET; void interface */
    case 0x437930:return frame_tank_initialize(e->read,e->write,e->data_ctx,entity,out);
    case 0x438cf0:return frame_tank_construct(e->read,e->write,e->data_ctx,entity,payload,&e->services,out);
    case 0x437b30:return frame_tank_destroy(e->read,e->write,e->data_ctx,entity,&e->services,out);
    case 0x421720:return frame_player_initialize(e->read,e->write,e->data_ctx,entity,&e->services,out);
    case 0x421790:return frame_player_construct(e->read,e->write,e->data_ctx,entity,payload,&e->services,out);
    case 0x4218d0:return frame_player_destroy(e->read,e->write,e->data_ctx,entity,&e->services,out);
    case 0x4302f0:return frame_tank_restore_route(e->read,e->write,e->data_ctx,entity,c->base.base.base.parameters.trig,e->services.work_limit,out);
    case 0x430a30: {
        physics_effect_environment effects={e->read,e->write,e->data_ctx,c->base.base.base.parameters.trig};
        physics_collision_services collision={physics_impact_particles_callback,&effects,e->services.work_limit};
        physics_obstacle_services obstacle={&collision,physical_method,physical_feedback,c};
        physics_vehicle_services services={&obstacle,physical_scene,c};
        if (!effects.trig) return -1;
        status=physics_vehicle_step_ambient(e->read,e->write,e->data_ctx,entity,effects.trig,&services,c->inactive_miss_words);
        if (!status) *out=entity;
        return status;
    }
    case 0x437c70:case 0x438970:
        status=frame_tank_hit(e->read,e->write,e->data_ctx,entity,payload,c->base.base.shatter.feedback,c->base.base.shatter.feedback_ctx);
        if (status) return status;
        *out=target==0x438970u ? entity : 0;return 0;
    case 0x438980:status=frame_tank_point_hit(e->read,e->write,e->data_ctx,entity,payload);break;
    case 0x4302c0:case 0x430560:case 0x4305b0: {
        physics_memory m={e->read,e->write,e->data_ctx,0};uint32_t record,o,value;
        record=pm_load(&m,pm_load(&m,0x55c694u,4)+entity*8u,4);o=record ? pm_load(&m,record+4u,4) : 0;
        if (target==0x4302c0u) pm_store(&m,o+0xde4u,payload==1u,1);
        else if (target==0x430560u) { value=pm_load(&m,o+0xd90u,1);pm_store(&m,o+0xd90u,payload==1u ? value&254u : value|1u,1); }
        else pm_store(&m,o+0xdeau,1u-pm_load(&m,o+0xdeau,1),1);
        status=m.error;break;
    }
    case 0x4350a0:return frame_tank_model_load(e->read,e->write,e->data_ctx,entity,payload,&e->services,c->base.base.base.parameters.shapes,out);
    case 0x437b00: {
        physics_memory m={e->read,e->write,e->data_ctx,0};uint32_t record,o;
        record=pm_load(&m,pm_load(&m,0x55c694u,4)+entity*8u,4);o=record ? pm_load(&m,record+4u,4) : 0;
        if (m.error) return m.error;
        if (!c->base.base.base.parameters.trig) return -1;
        status=physics_pose_update(e->read,e->write,e->data_ctx,o,c->base.base.base.parameters.trig);
        if (!status) *out=entity;
        return status;
    }
    case 0x40a3e0: {
        physics_obstacle_services services={0};physics_entity_environment environment;
        services.method=physical_method;services.ctx=c;
        environment.read=e->read;environment.write=e->write;environment.ctx=e->data_ctx;environment.trig=c->base.base.base.parameters.trig;
        environment.services=&services;environment.scene=physical_scene;environment.scene_ctx=c;
        return physics_known_entity_method(&environment,target,entity,payload,out);
    }
    case 0x40a9d0:return frame_fall_construct(e->read,e->write,e->data_ctx,entity,payload,&e->services,out);
    case 0x40ac30:status=frame_fall_destroy(e->read,e->write,e->data_ctx,entity,&e->services);break;
    case 0x40a5f0:status=frame_fall_tick(e->read,e->write,e->data_ctx,entity,&e->services,c->base.base.base.parameters.trig);break;
    case 0x42a440:return frame_smoker_construct(e->read,e->write,e->data_ctx,entity,payload,&e->services,out);
    case 0x42a060:status=frame_smoker_tick(e->read,e->write,e->data_ctx,entity);break;
    case 0x429fc0:status=frame_smoker_render(e->read,e->write,e->data_ctx,entity,&c->actors);break;
    case 0x41b740:return frame_noisy_construct(e->read,e->write,e->data_ctx,entity,payload,&e->services,out);
    case 0x41b870: {
        physics_memory m={e->read,e->write,e->data_ctx,0};uint32_t r=pm_load(&m,pm_load(&m,0x55c694u,4)+entity*8u,4),o=r ? pm_load(&m,r+4u,4) : 0;
        status=frame_noisy_tick(e->read,e->write,e->data_ctx,entity,e->services.work_limit);if (!status && o) frame_entity_sound(&e->services,0x41b870u,o,0,0);break;
    }
    case 0x442390:return frame_waypoint_construct(e->read,e->write,e->data_ctx,entity,payload,&e->services,out);
    case 0x43f670: {
        physics_memory m={e->read,e->write,e->data_ctx,0};uint32_t r,o,klass,unused;
        r=pm_load(&m,pm_load(&m,0x55c694u,4)+entity*8u,4);o=r ? pm_load(&m,r+4u,4) : 0;
        klass=pm_load(&m,0x55b6c4u,4);if (m.error) return m.error;
        status=frame_class_dispatch(e->read,e->data_ctx,klass,entity,e->services.method,e->services.method_ctx,&unused);if (status) return status;
        if (o) pm_store(&m,o+78u,65528,2);
        if (m.error) return m.error;
        *out=0;return 0;
    }
    case 0x41f720: {
        physics_memory m={e->read,e->write,e->data_ctx,0};uint32_t descriptor=pm_load(&m,0x545ed0u,4);if (m.error) return m.error;
        return frame_entity_dispatch(e->read,e->write,e->data_ctx,entity,descriptor,0,0,e->services.method,e->services.method_ctx,out);
    }
    case 0x504250: {
        uint32_t unused;status=frame_entity_activate(e->read,e->write,e->data_ctx,entity,e->services.method,e->services.method_ctx,&unused);
        if (!status) *out=entity;
        return status;
    }
    case 0x4041e0:return frame_entity_activate(e->read,e->write,e->data_ctx,entity,e->services.method,e->services.method_ctx,out);
    case 0x4041f0:case 0x503e40:
        status=frame_entity_queue(e->read,e->write,e->data_ctx,entity,0,e->services.allocate,e->services.release,e->services.system_ctx,e->services.work_limit);break;
    case 0x406280:case 0x43d250:return frame_camera_native(e,target,entity,has_payload,payload,out);
    case 0x504020:return frame_world_object_read(e->read,e->write,e->data_ctx,entity,payload,&e->services,&c->base.base.base.parameters,out);
    case 0x4298e0:return frame_weapon_spin_start(e->read,e->write,e->data_ctx,entity,out);
    case 0x41c230:status=frame_object_enter(e->read,e->write,e->data_ctx,entity,&e->services);break;
    case 0x41c270:status=frame_object_leave(e->read,e->write,e->data_ctx,entity,&e->services);break;
    case 0x41c300:status=frame_object_deactivate(e->read,e->write,e->data_ctx,entity,&e->services);break;
    case 0x41c7a0:status=frame_object_set_mode(e->read,e->write,e->data_ctx,entity,payload,&e->services);break;
    case 0x41c0a0:status=frame_object_auxiliary_create(e->read,e->write,e->data_ctx,entity,&e->services);break;
    case 0x41c190:status=frame_object_auxiliary_clear(e->read,e->write,e->data_ctx,entity,&e->services);break;
    case 0x41e960:status=frame_object_auxiliary_release(e->read,e->write,e->data_ctx,entity,&e->services);break;
    case 0x40ab90:status=frame_object_restore(e->read,e->write,e->data_ctx,entity,e->services.allocate,e->services.release,e->services.system_ctx,e->services.work_limit);break;
    case 0x422b50:status=frame_object_respawn(e->read,e->write,e->data_ctx,entity,e->services.release,e->services.system_ctx,e->services.work_limit);break;
    case 0x43f6a0:status=frame_ability_boost_start(e->read,e->write,e->data_ctx,entity);break;
    case 0x40aff0:status=frame_camera_finish_tick(e->read,e->write,e->data_ctx,entity,payload,&e->services);break;
    default:return frame_mesh_break_native(&c->base,target,entity,has_payload,payload,out);
    }
    if (!status) *out=0; /* void original methods: reserved, not EAX emulation */
    return status;
}
