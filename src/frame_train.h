#ifndef FRAME_TRAIN_H
#define FRAME_TRAIN_H
#include "frame_entity_factory.h"
#include "physics_attitude.h"
#include "frame_object_parameters.h"
int frame_train_construct(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t parser,const frame_entity_factory_services *entities,const frame_object_parameters_services *resources);
/*43F370 readable dispatcher for all ten movement callbacks. Class nameTrain
 * also represents orbiting/spinning/vertical/spring scenery. Logical callback
 * identities select C functions, never original instruction pointers. No audio.
 *43EDD0 retains both sound-related LCG draws because they affect game RNG. */
int frame_train_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *entities,const physics_trig *trig);
int frame_train_start(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *entities);
int frame_train_stop(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *entities);
#endif
