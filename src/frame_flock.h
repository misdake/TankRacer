#ifndef FRAME_FLOCK_H
#define FRAME_FLOCK_H
#include "frame_object_parameters.h"
int frame_flock_construct(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t parser,const frame_entity_factory_services *entities,const frame_object_parameters_services *resources);
int frame_flock_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *entities,const physics_trig *trig);
int frame_flock_start(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *entities);
int frame_flock_destroy(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *entities);
#endif
