#ifndef FRAME_VEHICLE_SCENE_H
#define FRAME_VEHICLE_SCENE_H
#include "frame_scene_effects.h"
#include "physics_attitude.h"
#include "frame_object_parameters.h"
/*43C400: four orbiting shield records, shared RNG target changes, angular
 * motion, transformed positions, size oscillation and texture spin. */
int frame_shield_orbit_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t object);
/*43B8D0: clear owned shared8-particle debris bank and object timers. */
int frame_vehicle_debris_clear(frame_data_read read,frame_data_write write,void *ctx,uint32_t object);
/*426440: owned2-bank following/fading geometry, UV/color frames and actual
 * light-pool retirement. Same-frame ownership returns from the whole method. */
int frame_vehicle_shared_fade(frame_data_read read,frame_data_write write,void *ctx,uint32_t object);
/*426180/426720: two oldest-slot pickup effects, exact pool ownership and
 * camera-oriented geometry. Projection preserves original sector asymmetry. */
int frame_pickup_effect_start(frame_data_read read,frame_data_write write,void *ctx,uint32_t source,uint32_t target);
int frame_pickup_effect_project(frame_data_read read,frame_data_write write,void *ctx,uint32_t target);
/*426E30 exact startup handles/styles/radial sectors, material name service. */
int frame_pickup_effect_initialize(frame_data_read read,frame_data_write write,void *ctx,frame_object_material_name material,void *material_ctx);
/*4427E0 headlight/reverse/brake quad state and pool ownership. */
int frame_vehicle_lights_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t object);
/* Complete4391E0 inactive shared state, in original synchronous order. */
int frame_vehicle_scene_inactive(frame_data_read read,frame_data_write write,void *ctx,uint32_t object,const physics_trig *trig);
/*43B940 shared debris update/emission. No audio device, but40F7B0's one
 * shared RNG advance is retained before the144 particle/color advances. */
int frame_vehicle_debris_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t object,uint32_t entity);
/*439EF0: eight track-spray particles, alternating contact side, surface style,
 * integration/damping/rotation, and at most one40ms emission. No RNG. */
int frame_vehicle_track_spray(frame_data_read read,frame_data_write write,void *ctx,uint32_t object);
/*439550 damage/traction smoke:15 records, damage-specific emitter priority,
 * shared RNG, transient transforms and qualified impact-line spawning. */
int frame_vehicle_damage_smoke(frame_data_read read,frame_data_write write,void *ctx,uint32_t object,const physics_trig *trig);
/*43A930 tyre-mark endpoint cache, joining quads and owned68-handle FIFO. */
int frame_vehicle_track_marks(frame_data_read read,frame_data_write write,void *ctx,uint32_t object);
/* Complete439170 active shared state. Audio disabled; RNG side effects kept. */
int frame_vehicle_scene_active(frame_data_read read,frame_data_write write,void *ctx,uint32_t object,uint32_t entity,const physics_trig *trig);
#endif
