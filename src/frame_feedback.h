#ifndef FRAME_FEEDBACK_H
#define FRAME_FEEDBACK_H
#include "frame_data.h"

enum { FRAME_EFFECT_SET_PERIODIC=1,FRAME_EFFECT_SET_CONDITION=2,FRAME_EFFECT_START=3,FRAME_EFFECT_SET_COLLISION_PERIODIC=4,FRAME_EFFECT_SET_DIRECTION=5 };
typedef struct frame_effect_command {
    uint32_t operation,effect,flags,argument0,argument1,parameter_count;
    uint32_t parameters[6];
} frame_effect_command;
/* Device output boundary; return 0 accepted, negative means host unavailable.
 * This callback never executes game rules or original instructions. */
typedef int (*frame_effect_sink)(void *ctx,const frame_effect_command *command);
/* Full 51D940/51E630 rule computation and 51DB70 surface condition selection. */
int frame_feedback_impact(frame_data_read read,frame_data_write write,void *ctx,
                            uint32_t strength,frame_effect_sink sink,void *sink_ctx);
int frame_feedback_surface(frame_data_read read,frame_data_write write,void *ctx,
                             frame_effect_sink sink,void *sink_ctx);
/*51DCA0 mine deployment pulse, including split-screen and device gates. */
int frame_feedback_mine(frame_data_read read,frame_data_write write,void *ctx,frame_effect_sink sink,void *sink_ctx);
/*51DCD0/51DD50: weapon-specific effect, player turret direction, shared
 * direction cache and the original edge-angle RNG draw. */
int frame_feedback_fire(frame_data_read read,frame_data_write write,void *ctx,uint32_t weapon,frame_effect_sink sink,void *sink_ctx);
/* Full51DA10; opcode4 packs magnitude,period,attackLevel,attackTime,fadeLevel,
 * fadeTime. Periodic offset/phase are0. Original flags180 include the envelope. */
int frame_feedback_collision(frame_data_read read,frame_data_write write,void *ctx,uint32_t kind,uint32_t alternate,
                                frame_effect_sink sink,void *sink_ctx);

/* Deterministic bounded DirectInput-effect simulation. Only explicitly mounted
 * effect tokens/types are accepted. No captured pointer is dereferenced by C. */
#define FRAME_EFFECT_CAPACITY 8u
typedef struct frame_effect_state {
    uint32_t effect,type,mounted,active,updates,starts;
    uint32_t parameters[6];
} frame_effect_state;
typedef struct frame_effect_host { frame_effect_state effects[FRAME_EFFECT_CAPACITY]; } frame_effect_host;
void frame_effect_host_init(frame_effect_host *host);
int frame_effect_host_mount(frame_effect_host *host,uint32_t effect,uint32_t type);
int frame_effect_host_submit(void *host,const frame_effect_command *command);
#endif
