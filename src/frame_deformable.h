#ifndef FRAME_DEFORMABLE_H
#define FRAME_DEFORMABLE_H
#include "frame_entity_factory.h"
#include "frame_shape.h"
/* 408170 in the single-player, no-audio scope. Compress nearby signed16 mesh
 * vertices in object-local units; preserve lateral drift, player material tag,
 * sound-trigger timer and both shared RNG advances. No voice is created.
 * Returns the original entity ID via out. DATA spans remain caller-checked. */
int frame_deformable_hit(frame_data_read read,frame_data_write write,void *ctx,
                           uint32_t entity,uint32_t vehicle,uint32_t *out);
/* 4079A0 defaults and 407760 exact mode==1 flag selection, original returns. */
int frame_deformable_initialize(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t *out);
int frame_deformable_set_flag(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t mode,uint32_t *out);
/* Full 407F70 with an explicitly silent voice policy: create/find return0 and
 * stop has no device state. Preserve F1 writes, F9 lifetime gate, record state
 * timers/fades and real C notification-list/pool work. Unknown DATA and exhausted
 * record/list budgets stop with a written prefix, not a successful short tick. */
int frame_deformable_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                            const frame_entity_factory_services *services,uint32_t *out);
/* Full407790/4077E0 membership and407F00 destructor, including native parent
 * dispatch, two scene lists, optional activation/notification and shape release. */
int frame_deformable_enter(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                             const frame_entity_factory_services *services,uint32_t *out);
int frame_deformable_leave(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                             const frame_entity_factory_services *services,uint32_t *out);
int frame_deformable_destroy(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                               const frame_entity_factory_services *services,uint32_t *out);
/* Full4079F0 parameter construction in no-audio scope, for cached/resolved
 * shapes. Parent is actual native game code. Cache miss/unknown parent is-5;
 * undefined reused parser temporary is-4, not a fabricated zero. */
int frame_deformable_construct(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t parser,
                                 const frame_entity_factory_services *services,uint32_t *out);
/* Same constructor with the complete cold-load file boundary for500D20. */
int frame_deformable_construct_resources(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t parser,const frame_entity_factory_services *services,const frame_shape_resources *resources,uint32_t *out);
#endif
