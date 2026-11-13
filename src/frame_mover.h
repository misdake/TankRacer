#ifndef FRAME_MOVER_H
#define FRAME_MOVER_H
#include "frame_explosion.h"
#include "frame_shatter.h"
/* Mover: proximity starts horizontal travel; optional terrain alignment,
 * endpoint stop/reverse/half-turn, and destructive collision. Audio disabled. */
int frame_mover_initialize(frame_data_read,frame_data_write,void *,uint32_t,const frame_entity_factory_services *);
int frame_mover_construct(frame_data_read,frame_data_write,void *,uint32_t,uint32_t,const frame_entity_factory_services *);
int frame_mover_tick(frame_data_read,frame_data_write,void *,uint32_t,const frame_entity_factory_services *,const physics_trig *);
int frame_mover_hit(frame_data_read,frame_data_write,void *,uint32_t,uint32_t,const frame_entity_factory_services *,const frame_explosion_services *,frame_effect_sink,void *);
#endif
