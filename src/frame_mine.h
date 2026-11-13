#ifndef FRAME_MINE_H
#define FRAME_MINE_H
#include "frame_shatter.h"
#include "frame_explosion.h"
/*414AD0 terrain-normal alignment. A terrain miss or null matrix leaves an
 * undefined original stack normal/result: return-4, never invent a normal. */
int frame_mine_align(frame_data_read read,frame_data_write write,void *ctx,uint32_t object,const physics_trig *trig);
/*414C60 complete single-player creation: actual factory, player position,
 * shape/bounds/alignment, triplet flags, lists/grid and30-mine cap. */
int frame_mine_spawn(frame_data_read read,frame_data_write write,void *ctx,uint32_t owner,uint32_t repeat,uint32_t type,const frame_entity_factory_services *entities,const physics_trig *trig,frame_effect_sink feedback,void *feedback_ctx,uint32_t *out);
/*414C20/415270 global lifetime list policy. Network sessions return-5. */
int frame_mine_enforce_limit(frame_data_read read,frame_data_write write,void *ctx,const frame_entity_factory_services *entities);
int frame_mine_clear(frame_data_read read,frame_data_write write,void *ctx,const frame_entity_factory_services *entities);
/* Other five class methods:4151E0/415220/415230/415290/414E40.
 * Full contact415050 is declared below with explicit explosion inputs. */
int frame_mine_construct(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t parser,const frame_entity_factory_services *entities,const physics_trig *trig);
int frame_mine_parent(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *entities);
int frame_mine_remove(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *entities);
int frame_mine_mark_inactive(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity);
int frame_mine_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *entities,const physics_trig *trig,frame_effect_sink feedback,void *feedback_ctx);
/*415050 single-player contact: arming history, true explosion creation,
 * native vehicle hit descriptor, camera/feedback and electro-shield ownership.
 * Requires explicit services.mine_collision_words on the explosion branch. */
int frame_mine_contact(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t vehicle,const frame_entity_factory_services *entities,const frame_explosion_services *effects,frame_effect_sink feedback,void *feedback_ctx);
#endif
