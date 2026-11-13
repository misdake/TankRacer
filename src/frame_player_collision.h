/* Pure state update after the driving-input region of 41F930.
 * No sound, force-feedback or graphics device operation occurs here. */
#ifndef FRAME_PLAYER_COLLISION_H
#define FRAME_PLAYER_COLLISION_H
#include "frame_data.h"

/* 442050: unsigned absolute magnitudes, max + quarter of each other component.
 * Result wraps at 32 bits and is returned signed; this is not Euclidean length. */
int32_t frame_collision_change_magnitude(const int32_t delta[3]);

/* 41FFB3..42009B: +107E collision-change accumulation and +118 countdown.
 * Called only along the enclosing player's driving path. The separate global
 * 53E3FC gate is checked here. Returns -1 for unavailable DATA; keep a failed
 * written prefix stopped. Does not replace the rest of 41F930. */
int frame_player_collision_state(frame_data_read read,frame_data_write write,
                                  void *ctx,uint32_t object);
#endif
