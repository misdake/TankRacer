#ifndef FRAME_ACTORS_H
#define FRAME_ACTORS_H
#include "frame_classes.h"
/* Graphics boundary for one sprite descriptor in DATA. Scene/style/particle
 * mutations occur before submission and remain native game rules. */
typedef int (*frame_actor_sprite_sink)(void *ctx,uint32_t descriptor);
typedef struct frame_actor_services { frame_actor_sprite_sink sprite;void *sprite_ctx; } frame_actor_services;
/*42A440,42A060,429FC0: complete Smoker initialization,16-particle simulation
 * and render submission. Rotation/lifetime wraps and shared10-entry sequence
 * are retained even when no browser frame is displayed. */
int frame_smoker_construct(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t parser,
                               const frame_entity_factory_services *entities,uint32_t *out);
int frame_smoker_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity);
int frame_smoker_render(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_actor_services *graphics);
/*41B740/41B870 no-audio execution: sound-resource/voice services are disabled
 * externally; list policy, parsed ranges and proximity selection remain C. */
int frame_noisy_construct(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t parser,
                              const frame_entity_factory_services *entities,uint32_t *out);
int frame_noisy_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t work_limit);
/*442390/442630/4417A0: WayPoint links, weather, limits, grid/global-list
 * membership and notification. Not an AI driving algorithm. */
int frame_waypoint_construct(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t parser,
                                 const frame_entity_factory_services *entities,uint32_t *out);
/*441BA0/441C80: derive WayPoint8x8 grid from actual world geometry and create
 * or destroy its64 lists. Width loops are signed/wrapping with a work budget. */
int frame_waypoint_grid_initialize(frame_data_read read,frame_data_write write,void *ctx,const frame_entity_factory_services *entities);
int frame_waypoint_grid_destroy(frame_data_read read,frame_data_write write,void *ctx,const frame_entity_factory_services *entities);
/*441860 resolves WayPoint symbols, marks alternate-route indices, numbers
 * the main circuit, selects halfway/backwards check and builds three weather rows. */
int frame_waypoint_link(frame_data_read read,frame_data_write write,void *ctx,const frame_entity_factory_services *entities);
/*404040/403F90: one-shot scenery Ufo/shake triggers and404200 named-target
 * resolution. These are scene rules; the Ufo itself uses its real C factory. */
int frame_background_construct(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t parser,const frame_entity_factory_services *entities);
int frame_background_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *entities);
int frame_background_link(frame_data_read read,frame_data_write write,void *ctx,const frame_entity_factory_services *entities);
#endif
