#ifndef FRAME_TIME_H
#define FRAME_TIME_H
#include "frame_data.h"

/* The system boundary supplies a millisecond counter. A scripted/replayed host
 * may return 0 when its input is exhausted. Game clock rules stay in C. */
typedef int (*frame_time_poll)(void *ctx,uint32_t *milliseconds);

/* Full 40BE00, including its ordered writes. 0 complete/suspended, 1 still
 * waiting after max_polls, -1 unavailable DATA, -4 unavailable system tick.
 * max_polls must be nonzero. Pending/failure is not a completed game frame. */
int frame_time_update(frame_data_read read,frame_data_write write,void *ctx,
                       frame_time_poll poll,void *poll_ctx,uint32_t max_polls);
/* Full 40BEF0: any nonzero argument suspends, zero resumes. */
int frame_time_suspend(frame_data_read read,frame_data_write write,void *ctx,
                        uint32_t suspend,frame_time_poll poll,void *poll_ctx);
/* 40F125..40F15A, after clock update: signed clamp of raw elapsed, then return
 * the optional game callback address for later C dispatch, never execute x86. */
int frame_time_physics_dt(frame_data_read read,frame_data_write write,void *ctx,
                           uint32_t *continuation);
#endif
