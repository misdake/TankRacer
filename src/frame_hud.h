#ifndef FRAME_HUD_H
#define FRAME_HUD_H
#include "frame_data.h"
#include "frame_commands.h"
#define FRAME_HUD_TEXTURE_BASE 0xf1000000u
#define FRAME_HUD_TEXTURES 21u
/*411330 single-player race HUD, with4113180 anchors and503320/503430 glyphs.
 * Caller uploads these original SUR resources under the reserved presentation
 * tokens. Resources live outside the game's allocator/DATA. */
const char *frame_hud_resource(uint32_t index,uint32_t map,uint32_t *pack);
int frame_hud_draw(frame_data_read read,void *ctx,frame_commands *commands);
/*412580 single-player CUP pause menu: native selection/confirmation/sliders. */
int frame_hud_pause_draw(frame_data_read read,void *ctx,frame_commands *commands);
int frame_hud_pause_draw_limited(frame_data_read,void *,frame_commands *,uint32_t);
/*4108A0 display side effect, separated from read-only command generation.
 * Consume the previous frame's event before the next update, even headless. */
int frame_hud_consume_events(frame_data_read read,frame_data_write write,void *ctx);
#endif
