#ifndef FRAME_SPIN_H
#define FRAME_SPIN_H
#include "frame_shatter.h"
int frame_spin_initialize(frame_data_read,frame_data_write,void *,uint32_t,const frame_entity_factory_services *);
int frame_spin_construct(frame_data_read,frame_data_write,void *,uint32_t,uint32_t,const frame_entity_factory_services *);
int frame_spin_hit(frame_data_read,frame_data_write,void *,uint32_t,uint32_t,const frame_entity_factory_services *,const physics_trig *,frame_effect_sink,void *);
int frame_spin_tick(frame_data_read,frame_data_write,void *,uint32_t,const frame_entity_factory_services *,const physics_trig *);
#endif
