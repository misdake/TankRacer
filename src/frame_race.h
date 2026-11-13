/* Pure race timing, transcribed from 4132E0. The host supplies elapsed time as
 * DATA; this module never polls a system clock or executes original code. */
#ifndef FRAME_RACE_H
#define FRAME_RACE_H
#include "frame_data.h"

/* Original-layout read/write interface, shared by native and future Wasm hosts.
 * Negative return means unavailable data; a written prefix must not be resumed. */
int frame_race_tick(frame_data_read read, frame_data_write write, void *ctx);
/*4132B0 countdown start and413270 reset the nine declared racer lap words. */
int frame_race_initialize(frame_data_write,void *);
int frame_race_reset_laps(frame_data_read,frame_data_write,void *);
#endif
