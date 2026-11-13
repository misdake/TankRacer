#ifndef FRAME_POWERUP_H
#define FRAME_POWERUP_H
#include "frame_shatter.h"
/*422F10 resource policy, using current [55AE40] namespace and the real shape
 * cache/decoder. Type0/6 remain untouched, as in the original. */
int frame_powerup_load_resources(frame_data_read read,frame_data_write write,void *ctx,const frame_entity_factory_services *entities,const frame_shape_resources *resources);
/*422620/422600: owned auxiliary header, type/name, difficulty/map radius,
 * deterministic initial tilt and sequence rollover. */
int frame_powerup_setup(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *entities,const physics_trig *trig);
int frame_powerup_select(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t object,const frame_entity_factory_services *entities,const physics_trig *trig);
/*422500: strict X/Z proximity, activation list and real spatial insertion. */
int frame_powerup_activate_near(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t target,const frame_entity_factory_services *entities,uint32_t *out);
/*422360 single-player branch. Network sessions return -5 before mutation. */
int frame_powerup_respawn_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t object,const frame_entity_factory_services *entities);
/*422050: falling/pivot, ground-normal convergence, settling flash and
 * camera-proximity spin, plus real respawn/animation dispatch. */
int frame_powerup_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *entities,const physics_trig *trig);
/*4227D0 single-player/no-audio constructor; invokes the real parent and
 * class descriptors. 421E60 pickup includes ammo, mesh-break, pose and effect. */
int frame_powerup_construct(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t parser,const frame_entity_factory_services *entities,const physics_trig *trig,uint32_t *out);
int frame_powerup_collect(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t vehicle,const frame_entity_factory_services *entities,frame_effect_sink feedback,void *feedback_ctx);
/*422C30/422D00: owned geometry backup/restore, live shape and triangle flags,
 * buffers/pool lifetime, original delay, pose/scene/grid restoration. */
int frame_powerup_capture(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *entities);
int frame_powerup_restore(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *entities);
/*422B10/423220: actual list/grid removal and disable/mesh-break dispatch. */
int frame_powerup_remove(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *entities);
int frame_powerup_disable(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *entities);
#endif
