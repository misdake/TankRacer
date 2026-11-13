#ifndef FRAME_MENU_H
#define FRAME_MENU_H
#include "frame_data.h"
#include "frame_commands.h"
/* Original GUIPCENG.GUI records: screen44/button64/control56/text80 bytes.
 * Only the external entry point is restricted to CUP; no HTML menu rules. */
#define FRAME_MENU_TEXTURE_BASE 0xf2000000u
typedef struct frame_menu_quad {
    uint32_t texture,layer,blend;
    int32_t x,y,width,height,u0,v0,u1,v1;
    uint32_t red,green,blue,scaled,corners[4];
} frame_menu_quad;
typedef struct frame_menu_draws { frame_menu_quad *quads;uint32_t count,capacity;int error;uint32_t pulse; } frame_menu_draws;
int frame_menu_load(frame_data_read,frame_data_write,void *,const unsigned char *,uint32_t);
int frame_menu_bootstrap(frame_data_write,void *);
int frame_menu_results_begin(frame_data_read,frame_data_write,void *);
int frame_menu_refresh(frame_data_read,frame_data_write,void *,uint32_t screen);
int frame_menu_direction(frame_data_read,frame_data_write,void *,uint32_t direction,uint32_t player);
int frame_menu_enter(frame_data_read,frame_data_write,void *,uint32_t screen,uint32_t fresh);
int frame_menu_update(frame_data_read,frame_data_write,void *,uint32_t event,uint32_t dt);
typedef void (*frame_menu_sound_sink)(void *,uint32_t cue,uint32_t variant);
int frame_menu_update_audio(frame_data_read,frame_data_write,void *,uint32_t,uint32_t,int32_t,frame_menu_sound_sink,void *);
int frame_menu_update_typed(frame_data_read,frame_data_write,void *,uint32_t event,uint32_t dt,int32_t character);
const unsigned char *frame_menu_characters(void);
int frame_menu_sample_input(frame_data_read,frame_data_write,void *,uint32_t bits,uint32_t *event);
int frame_menu_draw(frame_data_read,void *,frame_menu_draws *);
int frame_menu_draw_browser(frame_data_read,void *,frame_menu_draws *);
int frame_menu_submit(const frame_menu_draws *,const uint32_t *dimensions,frame_commands *);
uint32_t frame_menu_resources(void);
const char *frame_menu_resource(uint32_t index,uint32_t *identity);
#endif
