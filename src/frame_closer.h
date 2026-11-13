#ifndef FRAME_CLOSER_H
#define FRAME_CLOSER_H
#include "frame_shatter.h"
/*406390/4063E0/406460 including the three indirect DATA phase identities.
 * The identities select native state transitions, never original instructions. */
int frame_closer_construct(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t parser,const frame_entity_factory_services *entities,uint32_t *out);
int frame_closer_hit(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t vehicle,const frame_entity_factory_services *entities,frame_effect_sink feedback,void *feedback_ctx);
int frame_closer_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *entities,const physics_trig *trig);
#endif
