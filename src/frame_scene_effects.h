#ifndef FRAME_SCENE_EFFECTS_H
#define FRAME_SCENE_EFFECTS_H
#include "frame_data.h"
/*4FED50: initialize real160 quad and100 line pool markers/counts. */
int frame_scene_pools_initialize(frame_data_write,void *);
/* 4FEC50: allocate one 32-byte line record from the fixed 100-entry pool.
 * Position endpoints are copied in DWORD order, then arithmetic-shifted by3.
 * Exact returns: -1 if count==100; 100 if scan finds no free marker otherwise. */
int frame_line_allocate(frame_data_read read,frame_data_write write,void *ctx,uint32_t source,uint32_t *out);
/* Same pool operation with caller-owned endpoint/color values, for native
 * particle generators which do not create an emulated stack descriptor. */
int frame_line_allocate_values(frame_data_read read,frame_data_write write,void *ctx,
                                  const int32_t endpoints[6],const unsigned char colors[8],uint32_t *out);
/* Full 4FECD0 overwrite (no allocate/count change), DWORD forward copy. */
int frame_line_update(frame_data_read read,frame_data_write write,void *ctx,uint32_t id,uint32_t source);
int frame_line_update_values(frame_data_read read,frame_data_write write,void *ctx,uint32_t id,
                                const int32_t endpoints[6],const unsigned char colors[8]);
/* Full 4FED20 / 4FEC20. E0000000 is the retired marker, not a null handle.
 * Logical IDs use original uint32 address wrapping; DATA owner checks spans. */
int frame_line_release(frame_data_read read,frame_data_write write,void *ctx,uint32_t id);
int frame_light_release(frame_data_read read,frame_data_write write,void *ctx,uint32_t id);
/* Full 43B370: ordered 3x3 line releases, mode ID publication and timestamp0. */
int frame_temp_effects_expire(frame_data_read read,frame_data_write write,void *ctx,uint32_t mode);
/*43AF20 complete four-slot start/oldest eviction, nine line allocations and
 *60 RNG advances. Active owners only refresh their shared-slot timestamp. */
int frame_temp_effects_start(frame_data_read read,frame_data_write write,void *ctx,uint32_t object);
/* Full 43B3C0: three polyline groups, nine line updates and 60 shared RNG
 * advances per active call. Geometry/color DATA is meaningful scene state. */
int frame_temp_effects_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t object);
/*4FEAD0/4FEB50 fixed160-record projected-quad pool. Inputs are native
 * values, no emulated stack. Handles retain original full/inconsistent cases. */
int frame_light_allocate_values(frame_data_read read,frame_data_write write,void *ctx,const int32_t vertices[12],uint32_t material,const unsigned char style[12],uint32_t *out);
int frame_light_update_values(frame_data_read read,frame_data_write write,void *ctx,uint32_t id,const int32_t vertices[12]);
/*4FEAD0/4FEBD0 DATA forms: forward-copy all64 bytes before shifting12
 * coordinates. Replace also copies material/style, without changing count. */
int frame_light_allocate(frame_data_read read,frame_data_write write,void *ctx,uint32_t source,uint32_t *out);
int frame_light_replace(frame_data_read read,frame_data_write write,void *ctx,uint32_t id,uint32_t source);
#endif
