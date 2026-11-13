#ifndef FRAME_UFO_H
#define FRAME_UFO_H
#include "frame_shatter.h"
/*421BC0 player recovery: create once, otherwise mark/restart existing Ufo. */
int frame_ufo_player_request(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *entities);
/*Ufo is the attached recovery/abduction entity, separate from a flying Shell
 * of type2. Callback identities are C phase selectors, never executable PE. */
int frame_ufo_initialize(frame_data_read read,frame_data_write write,void *ctx);
int frame_ufo_reset(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity);
int frame_ufo_construct(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *entities);
int frame_ufo_stop(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *entities);
int frame_ufo_spawn(frame_data_read read,frame_data_write write,void *ctx,uint32_t vehicle,uint32_t offset,const frame_entity_factory_services *entities,uint32_t *out);
int frame_ufo_spawn_values(frame_data_read read,frame_data_write write,void *ctx,uint32_t vehicle,const int32_t offset[3],const frame_entity_factory_services *entities,uint32_t *out);
int frame_ufo_overlaps(frame_data_read read,void *ctx,uint32_t vehicle,uint32_t *out);
int frame_ufo_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *entities,const physics_trig *trig);
#endif
