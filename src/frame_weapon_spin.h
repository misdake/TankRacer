#ifndef FRAME_WEAPON_SPIN_H
#define FRAME_WEAPON_SPIN_H
#include "frame_data.h"
/* Full 441400 packed approach: signed16 rate*dt truncation, uint8 damping*dt
 * truncation and signed toward-zero decay. One final DATA scalar write. */
int frame_scalar_approach(frame_data_read read,frame_data_write write,void *ctx,uint32_t address,int32_t target,uint32_t packed);
/* Full 4298E0 activation, including BYTE counters, mode/player lifetime,
 * initial color masks, exactly three LCG steps and original EAX quotient. */
int frame_weapon_spin_start(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t *out);
/* Full 429A00 weapon-spin/effect state: phases, wrapped angle, three masked
 * color channels, exactly three LCG advances at a reached target, fade/hold and
 * BYTE global counters. work_limit bounds angle normalization writes. */
int frame_weapon_spin_update(frame_data_read read,frame_data_write write,void *ctx,uint32_t object,uint32_t work_limit);
#endif
