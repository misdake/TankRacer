#ifndef FRAME_CAMERA_H
#define FRAME_CAMERA_H
#include "frame_entity_factory.h"
#include "physics_attitude.h"
/*43D2A0 all Trackcam script modes and4057C0/406290 parent parameters. */
int frame_camera_parameters(frame_data_read read,frame_data_write write,void *ctx,uint32_t target,uint32_t entity,uint32_t parser,const frame_entity_factory_services *services);
/*43C700 full Trackcam state update. Computed direction matrices initialize
 * nine words but4F7300 publishes twelve; the three ambient words are explicit
 * caller inputs. NULL returns-4 only when that path actually consumes them.
 *50CD20 is exclusively the disabled audio listener, not a game callback. */
int frame_camera_track_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t camera,uint32_t racer,
                              const frame_entity_factory_services *services,const physics_trig *trig,const uint32_t matrix_tail[3]);
/*420F60 orbit/debug camera, with both relative-body and free-orbit branches. */
int frame_camera_player_view(frame_data_read read,frame_data_write write,void *ctx,uint32_t player,const physics_trig *trig);
/* Complete 4F7300: publish position>>3 and twelve transform words. Parameters
 * are logical DATA addresses; preserve aliasing/write order, raw32 slot index. */
int frame_camera_publish(frame_data_read read,frame_data_write write,void *ctx,uint32_t slot,uint32_t position,uint32_t matrix);
/*4055D0: camera shake mode/duration, signed WORD amplitude capped at2000. */
int frame_camera_shake(frame_data_read read,frame_data_write write,void *ctx,uint32_t camera,uint32_t amplitude,uint32_t duration);
/*42BCB0 and406110: converge the five starting-view channels, create the real
 * Chasecam at the next already-settled tick, and publish the old view that tick. */
int frame_camera_start_tick(frame_data_read,frame_data_write,void *,uint32_t,uint32_t,const frame_entity_factory_services *,const physics_trig *);
int frame_camera_start_to_chase(frame_data_read,frame_data_write,void *,uint32_t,uint32_t,uint32_t,const frame_entity_factory_services *);
/*4057F0: three chase views, input edges/look-back, angular smoothing and shake.
 * First-person local matrix has three explicit ambient caller words. */
int frame_camera_chase_tick(frame_data_read,frame_data_write,void *,uint32_t,uint32_t,const frame_entity_factory_services *,const physics_trig *,const uint32_t matrix_tail[3]);
/* Full 40AED0 / 43D150 creation and queued replacement of finish / Trackcam.
 * These create actual C object storage via registered constructors. They are
 * game rules, not graphics-device callbacks or a complete Trackcam updater. */
int frame_camera_create_finish(frame_data_read read,frame_data_write write,void *ctx,uint32_t racer,uint32_t slot,
                                 const frame_entity_factory_services *services);
int frame_camera_create_track(frame_data_read read,frame_data_write write,void *ctx,uint32_t target,uint32_t slot,
                                const frame_entity_factory_services *services);
/*442CA0 inverse Euler extraction from the original DATA lookup tables.
 * Keeps singular/null behavior; x86 signed division faults return-2. */
int frame_camera_matrix_angles(frame_data_read read,frame_data_write write,void *ctx,uint32_t matrix,uint32_t angles,uint32_t *nonsingular);
/* Same calculation into native values for C callers with an ordinary local
 * vector. NULL matrix leaves values untouched, just like442CA0. */
int frame_camera_matrix_angle_values(frame_data_read read,void *ctx,uint32_t matrix,uint32_t angles[3],uint32_t *nonsingular);
/* Full42BA90 start camera: native factory/replacement, Euler extraction,
 * selected distance preset and exactly one shared RNG step. */
int frame_camera_create_chase(frame_data_read read,frame_data_write write,void *ctx,uint32_t target,uint32_t slot,
                                const frame_entity_factory_services *services);
/* Full 40AFF0: timer/damping/rising motion, publish or Trackcam replacement.
 * racer is original EDX target; the camera slot is racer.signed8(+DF0). */
int frame_camera_finish_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t camera,uint32_t racer,
                               const frame_entity_factory_services *services);
/* Adds complete 406280/43D250 Camera/Chasecam/Trackcam initializer delegation;
 * everything else delegates to the portable base registry. Context is the
 * same frame_entity_native_context, never an original-instruction executor. */
int frame_camera_native(void *ctx,uint32_t target,uint32_t entity,uint32_t has_payload,uint32_t payload,uint32_t *out);
#endif
