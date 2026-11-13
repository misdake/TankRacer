#ifndef FRAME_PROJECTILE_H
#define FRAME_PROJECTILE_H
/*404590/404350 environment Bomber launcher: actual Shell factory, strict
 * proximity/rectangle gate, deterministic cooldown and both launch styles.
 * This is a world obstacle, not racer AI. Network58E7AC remains excluded. */
#include "frame_explosion.h"
#include "frame_feedback.h"
#include "frame_shatter.h"
int frame_projectile_emitter_spawn(frame_data_read read,frame_data_write write,void *ctx,uint32_t emitter_object,const frame_entity_factory_services *entities,uint32_t *out);
int frame_projectile_emitter_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *entities,const physics_trig *trig);
int frame_projectile_emitter_construct(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t parser,const frame_entity_factory_services *entities,const frame_object_parameters_services *resources);
int frame_projectile_initialize(frame_data_read read,frame_data_write write,void *ctx,frame_object_material_name material,void *material_ctx);
/*423280 complete shared explosion/projectile material initialization. */
int frame_projectile_materials(frame_data_read read,frame_data_write write,void *ctx,frame_object_material_name material,void *material_ctx);
/*4296B0 UFO geometry/offset resource, normal namespace-aware shape loader. */
int frame_projectile_model(frame_data_read read,frame_data_write write,void *ctx,const frame_entity_factory_services *entities,const frame_shape_resources *resources);
int frame_projectile_sprite(frame_data_read read,frame_data_write write,void *ctx,uint32_t object);
int frame_projectile_uv_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t object);
int frame_projectile_trail_emit(frame_data_read read,frame_data_write write,void *ctx,uint32_t object);
int frame_projectile_trail_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t object);
int frame_projectile_flash_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t object);
int frame_projectile_stop(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *entities);
/*427830: all six projectile types, real entity creation/list membership and
 * turret-relative target selection. Single player only. */
int frame_projectile_spawn(frame_data_read read,frame_data_write write,void *ctx,uint32_t owner,uint32_t type,const frame_entity_factory_services *entities,const physics_trig *trig,uint32_t *out);
typedef struct frame_projectile_hit {
    uint32_t entity,local_normal_valid;
    int32_t point[3],normal[3],local_normal[3],local_point[3];
} frame_projectile_hit;
/*423440: moving-racer relative sweep. The local normal can be published even
 * when a back-facing intersection is rejected; preserve that distinction. */
int frame_projectile_sweep(frame_data_read read,void *ctx,uint32_t entity,const int32_t old_position[3],frame_projectile_hit *out);
int frame_projectile_sweep_data(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t old_position,uint32_t point,uint32_t normal,uint32_t local_normal,uint32_t local_point,uint32_t *hit_entity);
/*429270 complete shield ricochet: RNG, optional nearby target, orientation,
 * ownership transfer and signed700/1024 energy retention. */
int frame_projectile_ricochet(frame_data_read read,frame_data_write write,void *ctx,uint32_t object,uint32_t vehicle,const physics_trig *trig);
/*421C60 weapon selection, byte ammo consumption and feedback. Sound absent. */
int frame_projectile_fire_select(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t object,const frame_entity_factory_services *entities,const physics_trig *trig,frame_effect_sink feedback,void *feedback_ctx,uint32_t *out);
/*42F8C0/421C10: cooldown, recoil and real parent dispatch. AI shield reaction
 * is deferred; eligible AI returns-5 at that boundary instead of success. */
int frame_projectile_tank_fire(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *entities,const physics_trig *trig,frame_effect_sink feedback,void *feedback_ctx,uint32_t *out);
int frame_projectile_player_fire(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *entities,uint32_t *out);
/*427220 single-player/no-AI impact: native origin/contact, actual explosion,
 * area impulse, screen flash, class-dispatched direct hit and camera shake. */
int frame_projectile_impact(frame_data_read read,frame_data_write write,void *ctx,uint32_t object,const int32_t origin[3],const uint32_t contact_words[25],uint32_t projectile_kind,const frame_entity_factory_services *entities,const frame_shatter_services *shatter,const frame_explosion_services *explosion);
/*42BEF0 terrain impact decal: two true plane queries, exclusion flag16,
 * exact four corners/UVs and24-entry oldest projected-quad queue. */
int frame_projectile_ground_mark(frame_data_read read,frame_data_write write,void *ctx,const int32_t origin[3]);
int frame_projectile_ground_initialize(frame_data_read read,frame_data_write write,void *ctx,frame_object_material_name material,void *material_ctx);
/*428680 complete single-player flight/steering/sweep/attachment/impact and
 * retirement. Rendering is separate; gameplay never delegates to PE code. */
int frame_projectile_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *entities,const physics_trig *trig,const frame_shatter_services *shatter,const frame_explosion_services *explosion);
#endif
