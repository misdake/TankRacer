#ifndef FRAME_ENVIRONMENT_H
#define FRAME_ENVIRONMENT_H
#include "frame_data.h"
#include "frame_explosion.h"
struct physics_trig;
/* Full 413A80: camera0 X/Z proximity, dt-scaled Q12 yaw, normalization every
 * 32 frames and active flag. Matrix writes are coalesced; final DATA and
 * arithmetic-fault prefixes match the original, not its transient stores. */
int frame_environment_player_view(frame_data_read read,frame_data_write write,void *ctx,const struct physics_trig *trig);
/* Full 441820: unsigned lap-distance lookup in three 64-byte rows. */
int frame_environment_at(frame_data_read read,void *ctx,uint32_t progress,uint32_t *flags);
/* Full 4F77D0/4FFD40: style, unsigned count cap300 and optional reset of Y in
 * all 300 shared environment-particle records. Other coordinates stay intact. */
int frame_environment_config(frame_data_read read,frame_data_write write,void *ctx,
                               uint32_t style,uint32_t count,uint32_t reset);
int frame_environment_reset(frame_data_read read,frame_data_write write,void *ctx);
/*42C2C0 weather: flash/thunder timers, quadrant selection and recursive bolts
 *42C060. Sound is disabled but40F760's RNG draw remains observable. */
int frame_environment_weather(frame_data_read,frame_data_write,void *,const struct physics_trig *,const frame_explosion_services *,uint32_t);
/* Full 5004D0: shift the active pool in DATA order and save the camera origin.
 * delta_address names three DATA words (may alias the pool); slot selects count
 * and camera record. -3 on work limit, -1 missing DATA. Prefix must stop. */
int frame_environment_shift(frame_data_read read,frame_data_write write,void *ctx,
                              uint32_t slot,uint32_t delta_address,uint32_t work_limit);
/* Full 42C800 rectangular region test, signed X/Z inclusive bounds. */
int frame_environment_in_zone(frame_data_read read,void *ctx,uint32_t object,uint32_t *inside);
/* Full 420EAA..420F2D, map5 zone-dependent shared 320-byte table switch. Copies
 * in DWORD order, including overlap; chooses DATA source 53E418/1C. */
int frame_player_palette_update(frame_data_read read,frame_data_write write,void *ctx,uint32_t object);
#endif
