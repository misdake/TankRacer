#ifndef FRAME_BOUNCE_H
#define FRAME_BOUNCE_H
#include "frame_shatter.h"
/* Complete Bounce405170/4051C0/404C30/404E00: real native parent,
 * OBPARAMS/overrides, impact transfer, airborne/rolling motion, terrain and
 * spatial/list retirement. Audio is disabled; its caller's RNG is retained. */
int frame_bounce_initialize(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *entities);
int frame_bounce_construct(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t parser,const frame_entity_factory_services *entities,uint32_t *out);
int frame_bounce_hit(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t vehicle,const frame_entity_factory_services *entities,const physics_trig *trig,frame_effect_sink feedback,void *feedback_ctx);
int frame_bounce_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *entities,const physics_trig *trig);
#endif
