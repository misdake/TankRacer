/* Device-independent input queries. Device polling supplies the DATA; these
 * functions implement the game's byte lookup and signed axis arithmetic. */
#ifndef FRAME_CONTROLS_H
#define FRAME_CONTROLS_H
#include "frame_data.h"

/* 5058F0 uses the full uint32 index, NOT code&255. Some callers pass 10A..115.
 * Returns -1 on unavailable data. Output is written only on success. */
int frame_control_key(frame_data_read read,void *ctx,uint32_t code,uint32_t *out);
/* 407010. All add/sub/shift arithmetic wraps at 32 bits before signed division.
 * Returns -2 for native IDIV divide-by-zero / quotient-overflow conditions.
 * No guessed axis-count clamp or fabricated zero is substituted for failures. */
int frame_control_axis(frame_data_read read,void *ctx,uint32_t argument,int32_t *out);
#endif
