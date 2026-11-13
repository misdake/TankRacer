#ifndef FRAME_PLAYER_STATE_H
#define FRAME_PLAYER_STATE_H
#include "frame_data.h"
#include "frame_entities.h"
/* Complete 421490, mode4's wrapping timer decrement. */
int frame_player_mode_timer(frame_data_read read,frame_data_write write,void *ctx,uint32_t object);
/* Full 42072C..420799: action12 key (including alternate binding), fire-method
 * dispatch and mode4 timer. Only pressed byte==1; other player slots skip key
 * queries. The actual weapon method must be registered native game logic. */
int frame_player_fire(frame_data_read read,frame_data_write write,void *ctx,uint32_t object,uint32_t entity,
                        frame_native_method method,void *method_ctx);
/* 420799..420AFD, called on the driving path, with the saved slot15 input.
 * Auxiliary braking modifies temporary traction/chassis parameters and restores
 * them through the qualified 421350 C helper. No interpretation as a device. */
int frame_player_aux_brake(frame_data_read read,frame_data_write write,void *ctx,
                            uint32_t object,uint32_t auxiliary_down);
/* 420B9E..420D8B: progress-decrease state machine and mode5 catch-up parameters.
 * Caller has already supplied +D18 progress (spatial calculation is separate).
 * -2 matches original IDIV exception; -1 unavailable DATA. Prefix must stop. */
int frame_player_progress_state(frame_data_read read,frame_data_write write,void *ctx,
                                 uint32_t object,uint32_t entity);
/* 420B03..420B9E: spatial score write and conditional route-link correction.
 * Mode2/4 skip this stage. The route search is the qualified 4420B0 C helper.
 * -3 exhausted search work; caller must stop, never substitute a score. */
int frame_player_route_update(frame_data_read read,frame_data_write write,void *ctx,
                                uint32_t object,uint32_t work_limit);
#endif
