#ifndef FRAME_PROGRESS_H
#define FRAME_PROGRESS_H
#include "frame_data.h"

typedef struct frame_checkpoint_result {
    uint32_t next_entity, finishes_lap;
} frame_checkpoint_result;

/* Complete 441CB0/442ED0 computation. Entity positions and checkpoint matrices
 * are DATA, not OS callbacks. side selects checkpoint's +78 + side*4 link.
 * The original caller's stack flag is represented by an explicit result.
 * No previous position is consulted: this is a half-space acceptance test. */
int frame_checkpoint_query(frame_data_read read,void *ctx,uint32_t racer_entity,
                            uint32_t checkpoint_entity,uint32_t side,
                            frame_checkpoint_result *out);
/* Full 441DC0, the same half-space geometry evaluated at a caller supplied
 * DATA position rather than the racer's current object. Used by AI lookahead. */
int frame_checkpoint_at(frame_data_read read,void *ctx,uint32_t position,uint32_t checkpoint_entity,
                           uint32_t side,frame_checkpoint_result *out);

/* Complete 4133E0 for a valid racer entity. Stores a packed 7-byte lap record:
 * minute/second/centisecond bytes then raw milliseconds, resets current elapsed
 * and increments count. count>=3 is a no-op; arithmetic and truncation are x86.
 * -1 unavailable DATA; -3 missing entity (original diagnostic/fault path).
 * A failed written prefix must stop. */
int frame_lap_record(frame_data_read read,frame_data_write write,void *ctx,
                      uint32_t racer_entity);

/* Full 4420B0: 8x8 checkpoint-grid selection and lap-relative progress score.
 * Unlike checkpoint acceptance this uses raw (unshifted) facing/width products,
 * a distinct 2D approximate distance, and strict first-in-list tie selection.
 * work_limit bounds malformed/huge lists/loops; -3 is incomplete, never a score.
 * -1 missing DATA. out is written only on success; no DATA writes occur. */
int frame_route_progress(frame_data_read read,void *ctx,int32_t x,int32_t z,
                           uint32_t checkpoint_entity,int32_t lap,uint32_t work_limit,int32_t *out);
/*441EC0: nearest valid checkpoint in the BSM-derived search window. */
int frame_route_nearest(frame_data_read read,void *ctx,uint32_t position,uint32_t work_limit,uint32_t *out);
#endif
