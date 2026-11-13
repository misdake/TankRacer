/* Deterministic host inputs and shipped archive access. OS I/O is outside this
 * module: native supplies read-only file bytes; a Wasm host supplies fetched bytes. */
#ifndef FRAME_HOST_H
#define FRAME_HOST_H
#include <stddef.h>
#include <stdint.h>

typedef struct frame_host_input {
    uint8_t keys[256], pressed[256], released[256];
    uint32_t raw_dt, sequence;
} frame_host_input;

int frame_host_sample_input(frame_host_input *state, const uint8_t *keys,
                            uint32_t count, uint32_t raw_dt, int focused);
/* FASTF101: 28-byte header, 24-byte entries, 12-byte names. Exact name match.
 * Validates the complete directory and every payload span before returning. */
int frame_host_archive_find(const void *file, uint32_t bytes, const char *name,
                            uint32_t name_bytes, uint32_t *offset, uint32_t *length);
/* Read-only virtual file mount for shipped SS shape bundles: total size/count,
 *12-byte directory (8-byte basename + offset), then length-prefixed raw models.
 * Accepts a bounded bare name or .sm filename, ASCII case-insensitive.
 * Entire directory is checked; duplicate matches are rejected. Same statuses
 * as archive_find. This is container I/O, not a substitute for model fixup. */
int frame_host_shape_find(const void *file,uint32_t bytes,const char *name,uint32_t name_bytes,uint32_t *offset,uint32_t *length);
/* Cursor arithmetic is shared by the native-file and fetched-memory hosts.
 * Seeking past EOF is legal; reads there return zero bytes. */
int frame_host_read_span(uint32_t bytes, uint32_t *cursor, uint32_t requested,
                         uint32_t *offset, uint32_t *length);
int frame_host_seek(uint32_t bytes, uint32_t *cursor, int32_t distance, uint32_t origin);
#endif
