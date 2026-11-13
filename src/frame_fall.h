#ifndef FRAME_FALL_H
#define FRAME_FALL_H
#include "frame_shatter.h"
/*40A9D0/40AC30: complete Fall parameter construction/references and teardown,
 * including network-session shape gates, actual parent class and linked list. */
int frame_fall_construct(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t parser,const frame_entity_factory_services *entities,uint32_t *out);
int frame_fall_destroy(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *entities);
/*40A5F0: impact-following yaw, settle-to-ground pitch, gravity/bounce/stop,
 * and actual auxiliary/shatter dispatch. No terrain or class method stubs. */
int frame_fall_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *entities,const physics_trig *trig);
#endif
