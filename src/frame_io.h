/* Platform simulation, separate from reverse-engineered game rules.
 * Resources are opaque IDs for caller-owned immutable bytes. No OS handles,
 * instruction emulator, host file path, wall clock or allocation is required. */
#ifndef FRAME_IO_H
#define FRAME_IO_H
#include <stdint.h>
#define FRAME_IO_MAX_FILES 64u

typedef struct frame_io_file {
    uint32_t handle, resource, length, cursor, live;
} frame_io_file;
typedef struct frame_io_files {
    frame_io_file files[FRAME_IO_MAX_FILES];
    uint32_t next_handle, last_error;
} frame_io_files;
typedef struct frame_io_span { uint32_t resource, offset, length; } frame_io_span;
typedef struct frame_io_clock { uint32_t tick, samples; } frame_io_clock;

void frame_io_files_init(frame_io_files *files);
/* 0 success; -1 invalid/closed handle or arguments; -2 invalid seek;
 * -3 bounded handle capacity exhausted. Failed operations preserve outputs and
 * cursors, update last_error to 6/87/131/4 as applicable. Handles never recycle.
 * Successful open/seek/size clear last_error; read/close leave it unchanged. */
int frame_io_open(frame_io_files *files,uint32_t resource,uint32_t length,uint32_t *handle);
int frame_io_close(frame_io_files *files,uint32_t handle);
int frame_io_size(frame_io_files *files,uint32_t handle,uint32_t *length);
int frame_io_read(frame_io_files *files,uint32_t handle,uint32_t requested,frame_io_span *out);
int frame_io_seek(frame_io_files *files,uint32_t handle,int32_t distance,uint32_t origin,uint32_t *position);

/* The scheduler advances virtual time; repeated system polls see the same tick.
 * Scripted polls may instead provide changing values to exercise busy-waiting.
 * No game dt clamp or pause policy lives in this external system counter. */
void frame_io_clock_init(frame_io_clock *clock,uint32_t tick);
void frame_io_clock_advance(frame_io_clock *clock,uint32_t elapsed);
int frame_io_clock_poll(void *ctx,uint32_t *tick);
#endif
