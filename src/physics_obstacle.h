#ifndef PHYSICS_OBSTACLE_H
#define PHYSICS_OBSTACLE_H
#include "physics_collision.h"
/* Bind known entity methods to independent scene implementations. fn identifies
 * original code for lookup only; it must never be executed. Return -6 for an
 * unsupported method. Both obstacle stages dispatch to the HIT entity. */
typedef int (*physics_entity_method)(void *ctx,uint32_t fn,uint32_t entity,uint32_t argument,uint32_t *result);
typedef int (*physics_force_feedback)(void *ctx,int32_t intensity);
typedef struct physics_obstacle_services {
    const physics_collision_services *collision;
    physics_entity_method method;
    physics_force_feedback feedback;
    void *ctx;
} physics_obstacle_services;
int physics_entity_collision_dispatch(physics_world_read read,physics_world_write write,void *ctx,
    uint32_t entity,uint32_t interface_address,uint32_t argument,const physics_obstacle_services *services,uint32_t *result);
int physics_obstacle_collision_step(physics_world_read read,physics_world_write write,void *ctx,
    uint32_t object,uint32_t entity,const physics_trig *trig,const physics_obstacle_services *services); /* 4324C0 */
int physics_deformable_collision_step(physics_world_read read,physics_world_write write,void *ctx,
    uint32_t object,uint32_t entity,const physics_trig *trig,const physics_obstacle_services *services); /* 4326D0 */
#endif
