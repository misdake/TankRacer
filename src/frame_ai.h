#ifndef FRAME_AI_H
#define FRAME_AI_H
#include "frame_entities.h"
#include "frame_entity_factory.h"
#include "physics_attitude.h"
#include "frame_shatter.h"
typedef struct frame_ai_services {
    const frame_entity_factory_services *entities;const physics_trig *trig;
    frame_effect_sink feedback;void *feedback_ctx;
} frame_ai_services;
int frame_ai_constants(frame_data_write,void *);
/* Qualified402440 throttle/brake and stuck-distance accumulation. */
int frame_ai_throttle(frame_data_read,frame_data_write,void *,uint32_t entity);
/*4020D0: predict a lookahead position, select route corridor/next target,
 * transform into local X/Z and apply the configured steering tiers. */
int frame_ai_steer(frame_data_read,frame_data_write,void *,uint32_t object,const physics_trig *);
/*402650 reverse/forward recovery phases and original angle scaling. */
int frame_ai_reverse(frame_data_read,frame_data_write,void *,uint32_t object,const physics_trig *);
/*402EC0 turn flag;403050 bounded aggression from a nearby racer. */
int frame_ai_turn_flag(frame_data_read,frame_data_write,void *,uint32_t object);
int frame_ai_anger_near(frame_data_read,frame_data_write,void *,uint32_t object,uint32_t other_object,int32_t amount,int32_t range);
/*403230 signed16 angle approach, period65520, including write-before-crossing
 * correction and the original returned residual. Not floating interpolation. */
int frame_ai_angle_approach(frame_data_read,frame_data_write,void *,int32_t target,uint32_t angle,int32_t rate,int32_t *residual);
/*401440/402050/402A10 far-vehicle velocity and heading. */
int frame_ai_far_drive(frame_data_read,frame_data_write,void *,uint32_t entity,const physics_trig *);
int frame_ai_far_refresh(frame_data_read,frame_data_write,void *,uint32_t entity,const physics_trig *);
/*401630 points the simplified chassis at its next waypoint.402D30 copies
 * a simplified pose back into the active physics representation. */
int frame_ai_realign(frame_data_read,frame_data_write,void *,uint32_t object,const physics_trig *);
int frame_ai_rejoin(frame_data_read,frame_data_write,void *,uint32_t entity);
/*4028C0 waypoint/lookahead/lap/progress update. Attract-demo lap-trigger
 * dispatch is outside the modes0/1 opponent scope and fails explicitly. */
int frame_ai_waypoints(frame_data_read,frame_data_write,void *,uint32_t entity,const physics_trig *,uint32_t work_limit);
/*403390 target selection,4036F0 target-relative yaw and4037C0 turret corridor. */
int frame_ai_choose_target(frame_data_read,frame_data_write,void *,uint32_t entity);
int frame_ai_target_yaw(frame_data_read,void *,uint32_t object,const physics_trig *,int32_t *yaw);
int frame_ai_target_corridor(frame_data_read,void *,uint32_t object,uint32_t *inside);
/*403770 sustained warning eligibility,403840 player's warning marker. */
int frame_ai_can_warn(frame_data_read,void *,uint32_t object,uint32_t *allowed);
int frame_ai_warn_player(frame_data_read,frame_data_write,void *);
/*401A90 fire scheduling and actual registered42F8C0 Tank fire dispatch. */
int frame_ai_fire(frame_data_read,frame_data_write,void *,uint32_t entity,const frame_entity_factory_services *);
/*403550/403110 aim, strict timing/angle gates and delayed warning/fire. */
int frame_ai_aim_fire(frame_data_read,frame_data_write,void *,uint32_t entity,int32_t error_angle,const physics_trig *,const frame_entity_factory_services *);
int frame_ai_attack_target(frame_data_read,frame_data_write,void *,uint32_t entity,const physics_trig *,const frame_entity_factory_services *);
/*403880 mine warning/held burst and402F00 aggression accumulation. */
int frame_ai_mine_warning(frame_data_read,frame_data_write,void *,uint32_t entity,const frame_entity_factory_services *);
int frame_ai_anger_tick(frame_data_read,frame_data_write,void *,uint32_t entity);
int frame_ai_weapon_tick(frame_data_read,frame_data_write,void *,uint32_t entity,const physics_trig *,const frame_entity_factory_services *);
/*4019D0/4018E0: react to a rival shot or a native explosion origin. */
int frame_ai_on_shot(frame_data_read,frame_data_write,void *,uint32_t owner,const frame_entity_factory_services *);
int frame_ai_on_blast(frame_data_read,frame_data_write,void *,const int32_t origin[3],uint32_t owner);
/*401290/413DD0: launch speed ramp and staggered neighbour refresh. */
int frame_ai_global_tick(frame_data_read,frame_data_write,void *,const physics_trig *);
/*402C60 wake-up mine policy;402AD0 active/simplified simulation transition. */
int frame_ai_wake(frame_data_read,frame_data_write,void *,uint32_t,const frame_ai_services *);
int frame_ai_visibility(frame_data_read,frame_data_write,void *,uint32_t,const frame_ai_services *);
/*401BD0 single-player AI update. Replay/attract special camera control fails
 * explicitly; Tank parent dispatch and projectile/mine creation are real C. */
int frame_ai_tick(frame_data_read,frame_data_write,void *,uint32_t,const frame_ai_services *);
/*425930 actual AI*.DAT syntax. index is vehicle-1,0..14. */
int frame_ai_configure_span(frame_data_read,frame_data_write,void *,uint32_t index,const unsigned char *,uint32_t length,uint32_t work_limit,uint32_t *consumed);
/*403A50 selection reset; wall-clock srand seed is an explicit host input. */
int frame_ai_selection_reset(frame_data_read,frame_data_write,void *,uint32_t seed);
int frame_ai_select_vehicle(frame_data_read,frame_data_write,void *,uint32_t work_limit,uint32_t *vehicle,uint32_t *colour);
int frame_ai_construct(frame_data_read,frame_data_write,void *,uint32_t entity,uint32_t parser,const frame_entity_factory_services *,const frame_shape_resources *);
/*403D60 parent gate and403D90/403E50 confusion wrappers. */
int frame_ai_parent(frame_data_read,void *,uint32_t,const frame_entity_factory_services *,uint32_t *out);
int frame_ai_hit(frame_data_read,frame_data_write,void *,uint32_t entity,uint32_t payload,uint32_t shell,const frame_entity_factory_services *);
int frame_ai_hit_pair(frame_data_read,frame_data_write,void *,uint32_t entity,uint32_t payload,uint32_t type,frame_native_method_pair,void *method_ctx);
#endif
