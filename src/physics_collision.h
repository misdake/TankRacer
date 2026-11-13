#ifndef PHYSICS_COLLISION_H
#define PHYSICS_COLLISION_H
#include "physics_vector.h"

/* A synchronous, stateful scene operation, NOT a disposable render notice.
 * 42A5C0 owns shared RNG, particle records and +1082. The callback must apply
 * those writes to the SAME data world before returning zero. No callback when
 * required returns -5; a negative callback result aborts the current stage.
 * record is the original contact record (its +28 is consumed by the effect).
 * A failed stage may have written an earlier prefix; the host must stop. */
typedef int (*physics_impact_effect)(void *ctx,uint32_t object,uint32_t record,int32_t kind);
typedef struct physics_collision_services {
    physics_impact_effect impact;
    void *ctx;
    uint32_t separation_limit; /* explicit host work bound; zero means 65536 */
} physics_collision_services;

uint32_t physics_collision_direction(int32_t x,int32_t z); /* 42D800 */
int physics_tanks_approaching(physics_world_read read,void *ctx,uint32_t self,uint32_t other); /* 42D860 */
/* 42D9A0: returns 0 no overlap,1 handled, negative error. */
int physics_tank_overlap_response(physics_world_read read,physics_world_write write,void *ctx,
    uint32_t self,uint32_t other,int32_t relative_speed,const physics_collision_services *services);
/* 42CE20: full query/overlap/approach/fallback response and +10C6 timer. */
int physics_tank_collision_step(physics_world_read read,physics_world_write write,void *ctx,
    uint32_t object,const physics_collision_services *services);
/* 432190: ordered four-point ground responses, yaw and track damage. */
int physics_ground_collision_step(physics_world_read read,physics_world_write write,void *ctx,
    uint32_t object,const physics_trig *trig,const physics_collision_services *services);
#endif
