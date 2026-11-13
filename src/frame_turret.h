#ifndef FRAME_TURRET_H
#define FRAME_TURRET_H
#include "frame_data.h"

/* 420454..42072C inside the driving branch of 41F930. Updates turret velocity,
 * signed16 angle, two independent key flags, recenter latch/cooldown and dirty
 * flag. Excludes subsequent fire dispatch. All arithmetic is original integer
 * arithmetic; no device polling, mesh rendering or original-code callback. */
int frame_turret_input(frame_data_read read,frame_data_write write,void *ctx,uint32_t object);
#endif
