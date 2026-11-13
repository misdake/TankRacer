#ifndef FRAME_AUDIO_H
#define FRAME_AUDIO_H
#include "frame_data.h"
#include <stddef.h>
/* Audio is an external sink. Never allocate voices inside game DATA or use
 * playback completion to decide gameplay. IDs below identify original cues. */
typedef void (*frame_audio_sink)(void *,uint32_t cue,uint32_t object,uint32_t variant,const int32_t *position,const unsigned char *name);
typedef struct frame_audio_command {
    uint32_t operation,key,sample,flags,gain,frequency;
    int32_t position[3];
} frame_audio_command;
size_t frame_audio_size(void);
void frame_audio_initialize(void *,frame_data_read,void *,uint32_t cup);
void frame_audio_begin(void *);
/*50E3B0 menu resources; cue is the original helper identity. */
void frame_audio_gui(void *,uint32_t cue,uint32_t variant);
void frame_audio_cue(void *,uint32_t,uint32_t,uint32_t,const int32_t *,const unsigned char *);
int frame_audio_tick(void *);
uint32_t frame_audio_count(void *);
const frame_audio_command *frame_audio_commands(void *);
#endif
