#ifndef PHYSICS_INACTIVE_H
#define PHYSICS_INACTIVE_H
#include "physics_step.h"
/* Returns -4 on terrain miss: native consumes unwritten caller-stack normals. */
int physics_inactive_contact(physics_world_read read,physics_world_write write,void *ctx,uint32_t object,const physics_trig *trig);
/* Same4333C0 with explicit incoming uninitialized normal slots on a terrain
 * miss. These are inputs, not a guessed up normal or fallback query output. */
int physics_inactive_contact_ambient(physics_world_read,physics_world_write,void *,uint32_t,const physics_trig *,const int32_t miss_words[3]);
int physics_inactive_attitude(physics_world_read read,physics_world_write write,void *ctx,uint32_t object,const physics_trig *trig);
int physics_inactive_pose(physics_world_read read,physics_world_write write,void *ctx,uint32_t object);
int physics_inactive_hull(physics_world_read read,physics_world_write write,void *ctx,uint32_t object);
int physics_inactive_step(physics_world_read read,physics_world_write write,void *ctx,uint32_t object,uint32_t entity,const physics_trig *trig,const physics_vehicle_services *services);
int physics_inactive_step_ambient(physics_world_read,physics_world_write,void *,uint32_t object,uint32_t entity,const physics_trig *,const physics_vehicle_services *,const int32_t miss_words[3]);
#endif
