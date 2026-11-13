#ifndef PHYSICS_EFFECTS_H
#define PHYSICS_EFFECTS_H
#include "physics_vector.h"
/* Complete 42A5C0 state operation: fixed particle ring, shared RNG, endpoint
 * geometry, fixed renderer pool allocation/free and object timer side effect.
 * Reads original-layout DATA only. No renderer calls or native execution. */
int physics_impact_particles(physics_world_read read,physics_world_write write,void *ctx,
    uint32_t object,uint32_t record,int32_t kind,const physics_trig *trig);
/* Full42AAC0: animate owned impact rings, line geometry and60ms retirement.
 * Same-frame ring returns from the entire function after +1082 update.
 * A required offset with a null matrix is undefined original stack input:-4. */
int physics_impact_particles_tick(physics_world_read read,physics_world_write write,void *ctx,uint32_t object,const physics_trig *trig);
typedef struct physics_effect_environment {
    physics_world_read read;
    physics_world_write write;
    void *ctx;
    const physics_trig *trig;
} physics_effect_environment;
/* Bind directly to physics_collision_services.impact. */
int physics_impact_particles_callback(void *ctx,uint32_t object,uint32_t record,int32_t kind);
#endif
