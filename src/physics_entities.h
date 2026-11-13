#ifndef PHYSICS_ENTITIES_H
#define PHYSICS_ENTITIES_H
#include "physics_step.h"
typedef struct physics_entity_environment {
    physics_world_read read;
    physics_world_write write;
    void *ctx;
    const physics_trig *trig;
    const physics_obstacle_services *services;
    physics_scene_operation scene;
    void *scene_ctx;
} physics_entity_environment;
/* Independent known collision-method bodies. Activation/list allocation,
 * audio ownership and spatial-list removal remain explicit scene services.
 * Unknown fn identifiers return -6, never invoke arbitrary machine code. */
int physics_known_entity_method(void *ctx,uint32_t fn,uint32_t entity,uint32_t argument,uint32_t *result);
#endif
