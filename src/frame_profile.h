#ifndef FRAME_PROFILE_H
#define FRAME_PROFILE_H
#include "frame_data.h"
#define FRAME_PROFILE_BYTES 0x1378u
typedef int (*frame_profile_feedback)(void *,uint32_t enabled,uint32_t *acquired);
typedef struct frame_profile_devices { uint32_t audio_settings;frame_profile_feedback feedback;void *ctx; } frame_profile_devices;
/*40DDD0/40DFE0 profile format0x66, without filesystem calls or compiler-local
 * stack storage. Unwritten padding is retained from the caller's output span;
 * initialize it explicitly (native host uses zero). Invalid size/version does
 * not publish DATA. Device operations are explicit acquire/release/apply edges. */
int frame_profile_encode(frame_data_read,void *,const frame_profile_devices *,unsigned char *,uint32_t);
int frame_profile_decode(frame_data_read,frame_data_write,void *,const frame_profile_devices *,const unsigned char *,uint32_t);
#endif
