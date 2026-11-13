/* Screen overlay state, 4F7390. This is game/presentation DATA, not a device
 * force-feedback call. 4F7130/4FE9E0 consume these alpha/RGB/blend settings. */
#ifndef FRAME_OVERLAY_H
#define FRAME_OVERLAY_H
#include "frame_data.h"
#include "frame_commands.h"

typedef struct frame_overlay_params {
    uint32_t blend_mode, alpha, red, green, blue;
} frame_overlay_params;

/* enable==0 changes only the active flag. Raw values and unsigned slot address
 * arithmetic are preserved; colour truncation occurs later in the renderer. */
int frame_overlay_set(frame_data_read read, frame_data_write write, void *ctx,
                      uint32_t enable, uint32_t slot, const frame_overlay_params *params);
/*4F7130 ->4FE9E0, AFTER scene and HUD. Whole-view flash/tint; no DATA writes.
 * mode1 uses the original additive blend, other modes use source alpha. */
int frame_overlay_draw(frame_data_read read,void *ctx,frame_commands *commands,uint32_t slot);
#endif
