#ifndef FRAME_ABILITY_BOOST_H
#define FRAME_ABILITY_BOOST_H
#include "frame_entities.h"
/* Full 43F6A0 activation and 437BF0 drive boost/timer application. */
int frame_ability_boost_start(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity);
int frame_ability_boost_apply(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t *out);
/* Complete 43FA60 (variant0) / 43F7E0 (variant1) camera phase rules. Methods
 * are registered native game code; null registry supports only known437BF0. */
int frame_ability_boost_camera(frame_data_read read,frame_data_write write,void *ctx,uint32_t object,uint32_t variant,
                                 frame_native_method method,void *method_ctx);
/* Complete 43F720: player/nonplayer gates, camera phases, native drive boost
 * dispatch and once-per-frame shared mesh UV/RNG update. No graphics API. */
int frame_ability_boost_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t object,
                               frame_native_method method,void *method_ctx);
#endif
