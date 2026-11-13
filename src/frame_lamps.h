#ifndef FRAME_LAMPS_H
#define FRAME_LAMPS_H
#include "frame_object_parameters.h"
#include "frame_actors.h"
/*413520 file-driven lamp instances. Byte acquisition is external; the actual
 * comment/decimal parser and packed24-byte lamp records remain game rules. */
int frame_lamps_construct(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t parser,const frame_entity_factory_services *entities,const frame_object_parameters_services *resources);
/*413890: periodic camera visibility and flashing, no renderer callbacks. */
int frame_lamps_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t work_limit);
/*413A10: preserve descriptor writes and emit through the explicit sprite sink. */
int frame_lamps_render(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t work_limit,const frame_actor_services *graphics);
#endif
