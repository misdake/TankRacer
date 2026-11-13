#ifndef FRAME_VEHICLE_RENDER_H
#define FRAME_VEHICLE_RENDER_H
#include "frame_data.h"
/* Original vehicle draw helpers. Writes are presentation scratch (including
 * mesh colours and render-only RNG); callers must provide a private overlay.
 * Matrix has twelve words, position retains the original 1/8 world units.
 * blend1 means4F7550(1): PC alpha128, capability8000 additive. */
typedef struct frame_vehicle_render_services {
    int (*sprite)(void *,uint32_t descriptor);
    int (*mesh)(void *,uint32_t shape,const int32_t position[3],const int32_t matrix[12],uint32_t blend1);
    void *ctx;
} frame_vehicle_render_services;
/*42A500 display template and40D346's shared smoke material identity.
 *5466A4 is the same cached501290(smoke2/pcsmoke,1) loaded by438D60. */
int frame_vehicle_smoke_templates(frame_data_read,frame_data_write,void *);
/* Draw-to-next-frame state from433D10: DEC HUD gate and camera78 local
 * turret offset. Run after each logical update, before the next camera tick;
 * GPU draws and repeated zero-step redraws must remain read-only. */
int frame_vehicle_view_feedback(frame_data_read,frame_data_write,void *,uint32_t object);
/*433D10->4F7530: per-part environment layer selection,0/off,1/model,
 *2/model concatenated with published view (during the starting sequence). */
int frame_vehicle_self_layer(frame_data_read,void *,uint32_t object,uint32_t part,uint32_t *mode);
/*433D6A..433EAF: the selected player's third view replaces the normal
 * vehicle dispatcher. handled is zero for every other owner/view. */
int frame_vehicle_cockpit_draw(frame_data_read,frame_data_write,void *,uint32_t object,uint32_t *handled,const frame_vehicle_render_services *);
int frame_vehicle_brake_lights_draw(frame_data_read,frame_data_write,void *,uint32_t object,const frame_vehicle_render_services *); /*4392F0*/
int frame_vehicle_reverse_lights_draw(frame_data_read,frame_data_write,void *,uint32_t object,const frame_vehicle_render_services *); /*439430*/
/*433D10 case7: four segments, turret socket, hull frame and decaying lean.
 * mesh callback inherits the owner's already locked bucket/style/shade. */
int frame_vehicle_aerial_draw(frame_data_read,frame_data_write,void *,uint32_t object,const frame_vehicle_render_services *);
int frame_vehicle_boost_draw(frame_data_read,frame_data_write,void *,uint32_t object,const frame_vehicle_render_services *); /*43FD40*/
int frame_vehicle_shield_draw(frame_data_read,frame_data_write,void *,uint32_t object,const frame_vehicle_render_services *); /*429EE0*/
int frame_vehicle_orbs_draw(frame_data_read,frame_data_write,void *,uint32_t object,const frame_vehicle_render_services *); /*43B740*/
int frame_vehicle_mine_glow_draw(frame_data_read,frame_data_write,void *,uint32_t object,const frame_vehicle_render_services *); /*43C650*/
#endif
