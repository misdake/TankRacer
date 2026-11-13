/* The driving/countdown INPUT regions inside 41F930, not its complete player
 * lifecycle, weapons, lap progression, AI or base-Tank dispatch. No OS polling. */
#ifndef FRAME_PLAYER_INPUT_H
#define FRAME_PLAYER_INPUT_H
#include "frame_data.h"

typedef struct frame_player_input_rates {
    /* Captured at 41F957..41F977 on entry to the enclosing player update.
     * 441370 interprets each raw word as SIGNED int16; these are not constants. */
    uint16_t throttle_return, brake_return;
} frame_player_input_rates;

typedef struct frame_player_input_result {
    uint32_t driving;          /* nonzero +DEA branch; selects continuation */
    uint32_t auxiliary_down;   /* action slot15, live caller-local at esp+1C */
} frame_player_input_result;

/* 421350: recover the three temporary parameter overrides, including signed
 * 16-bit wrap and the mode4 rearm field. No device or original-code callback. */
int frame_player_restore_overrides(frame_data_read read,frame_data_write write,
                                   void *ctx,uint32_t object);

/* Start at 41FAEA with the caller's +DEA decision already made.
 * Driving path ends BEFORE 41FFB3; countdown/mode4 path BEFORE 420AFD or
 * 420B03 (the start-stage4 branch bypasses the register-restoration join).
 * Uses qualified full-index key/axis queries. -1: unavailable DATA; -2: native
 * IDIV exception. A failed update may have written a prefix; stop, do not retry.
 * The result is written only on success. It is not a CPU-register image. */
int frame_player_input_step(frame_data_read read,frame_data_write write,void *ctx,
                            uint32_t object,const frame_player_input_rates *rates,
                            frame_player_input_result *out);
#endif
