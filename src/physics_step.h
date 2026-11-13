#ifndef PHYSICS_STEP_H
#define PHYSICS_STEP_H
#include "physics_obstacle.h"
enum physics_scene_phase {
    PHYSICS_SCENE_PLAYER_VIEW=1, /* Reserved legacy ID; view rules now run in C. */
    PHYSICS_SCENE_TEMP_EXPIRE=2, /* Reserved legacy ID; expiration now runs in C. */
    PHYSICS_SCENE_CONTACT_FEEDBACK=3,
    PHYSICS_SCENE_AFTER_POSE=4,
    PHYSICS_SCENE_INACTIVE_EFFECTS=5,
    PHYSICS_SCENE_OBJECT_SOUND=6,
    PHYSICS_SCENE_ENTITY_ACTIVATE=7,
    PHYSICS_SCENE_REMOVE_GRID=8,
    PHYSICS_SCENE_FORCE_FEEDBACK=9
};
/* Scene operations share DATA with the vehicle. They must finish before the
 * next stage, not be dropped or delayed. No instruction dispatch exists here.
 * Missing required scene implementation returns -8. */
typedef int (*physics_scene_operation)(void *ctx,int phase,uint32_t object,uint32_t entity,int32_t argument);
typedef struct physics_vehicle_services {
    const physics_obstacle_services *obstacle;
    physics_scene_operation scene;
    void *ctx;
} physics_vehicle_services;
/* Active/inactive 430A30 vehicle update. All forces, collision response, velocity and
 * position integration, attitude, body/track poses and rescale are independent
 * C, including complete 429A00 weapon-spin/effect and 43F720 boost ability
 * state. Boost dispatch accepts the registered 437BF0 C drive-boost method;
 * unknown class methods return -5 instead of executing original code.
 * Scene services remain separately owned, including their RNG and data writes.
 * Error may follow a written prefix; caller must stop and reset the world. */
int physics_vehicle_step(physics_world_read read,physics_world_write write,void *ctx,
    uint32_t entity,const physics_trig *trig,const physics_vehicle_services *services);
int physics_vehicle_step_ambient(physics_world_read,physics_world_write,void *,uint32_t entity,const physics_trig *,const physics_vehicle_services *,const int32_t inactive_miss_words[3]);
#endif
