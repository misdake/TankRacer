#ifndef FRAME_PLAYER_LIFECYCLE_H
#define FRAME_PLAYER_LIFECYCLE_H
#include "frame_camera.h"
#include "physics_attitude.h"
/*40F8F0 gameplay mixed into sound: horn edge/timer and other-vehicle +10C6.
 * Disabled audio handles/descriptor flags are deliberately outside this API;
 * no game state is inferred from whether an audio sample is playing. */
int frame_player_horn(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity);
/* Complete 4010D0 takeover: copy state into precreated AI entity, repair scene
 * references/slots, transfer list membership, switch selected racer and invoke
 * its registered native update. This does not supply the AI algorithm itself.
 * Unknown game method remains -5; it cannot be treated as an OS/device sink. */
int frame_player_takeover(frame_data_read read,frame_data_write write,void *ctx,
                             const frame_entity_factory_services *services);
/* Complete pure AI helpers 401F50 / 401330 / 401FC0: progress rank, nearest
 * AI-neighbor classification and spacing throttle. These are game rules;
 * they do not stand in for the complete 401BD0 AI update/steering algorithm. */
int frame_ai_rank(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity);
int frame_ai_neighbor(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity);
int frame_ai_spacing_throttle(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity);
/* Complete 402820 route branch selection: signed8 lap tags, special tag2,
 * and exactly one LCG advance only when both branches have unspecified tag-1. */
int frame_ai_route_branch(frame_data_read read,frame_data_write write,void *ctx,uint32_t object,
                             uint32_t checkpoint,uint32_t *out);
/*437930/438CF0/437B30: Tank initial state, actual parent parameters/pose/list
 * placement, and owned model release followed by real parent destruction. */
int frame_tank_initialize(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t *out);
int frame_tank_construct(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t parser,const frame_entity_factory_services *entities,uint32_t *out);
int frame_tank_destroy(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *entities,uint32_t *out);
/*421720 player subclass initialization, including partial-width shared input
 * reset. Complete421790 model/input-file configuration remains separate. */
int frame_player_initialize(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *entities,uint32_t *out);
/*421790 full player configuration: real parent, :player selector, selected
 * vehicle file dispatch, pose, start camera, and mode3 private arena storage.
 * Missing selector value has undefined original stack input and returns-4. */
int frame_player_construct(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t parser,const frame_entity_factory_services *entities,uint32_t *out);
int frame_player_destroy(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *entities,uint32_t *out);
/*4302F0 checkpoint recovery: walk backward past disabled/blocked route nodes,
 * adjust lap, move above selected checkpoint, face route and refresh pose. */
int frame_tank_restore_route(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const physics_trig *trig,uint32_t work_limit,uint32_t *out);
/*41F7A0: runtime player factory, single-player/split selector, actual lists. */
int frame_player_create(frame_data_read read,frame_data_write write,void *ctx,uint32_t slot,const frame_entity_factory_services *entities,uint32_t *out);
/*4305E0: assign nearest starting route for all declared racers, no AI tick. */
int frame_player_routes_initialize(frame_data_read read,frame_data_write write,void *ctx,uint32_t work_limit);
/*403A10 storage initializer and401000 precreated post-race owner. These
 * contain no AI steering/tick algorithm. Invalid cup has undefined caller
 * ambient data in the original and returns-4. */
int frame_player_finish_owner_initialize(frame_data_read,frame_data_write,void *,uint32_t,const frame_entity_factory_services *,uint32_t *);
int frame_player_finish_owner_create(frame_data_read,frame_data_write,void *,const frame_entity_factory_services *);
/*413DF0..413E1B: pending takeover activation and stale input latch reset. */
int frame_player_finish_owner_activate(frame_data_read,frame_data_write,void *,const frame_entity_factory_services *);
#endif
