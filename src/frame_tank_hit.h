#ifndef FRAME_TANK_HIT_H
#define FRAME_TANK_HIT_H
#include "frame_feedback.h"
/*437C70 complete contact-record response and4383A0 local velocity impulse:
 * angular/linear response, mode damage, health display, model deformation and
 * named DI output. All contact payload bytes are inputs; no external game hook. */
int frame_tank_hit(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t contact,frame_effect_sink feedback,void *feedback_ctx);
/*438440 shell-specific wrapper: suspension kick/damping, full temporary
 * polyline effect allocation or refresh, then6000ms owner timer. */
int frame_tank_shell_hit(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t contact,frame_effect_sink feedback,void *feedback_ctx);
/* Same contact rules for an explicit native100-byte record (25 little-endian
 * words): position, velocity, world/local normals, world/local hit points,
 * target entity, eight packed coefficient WORDs, projectile kind and age. */
int frame_tank_hit_values(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const uint32_t contact_words[25],frame_effect_sink feedback,void *feedback_ctx);
int frame_tank_shell_hit_values(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const uint32_t contact_words[25],frame_effect_sink feedback,void *feedback_ctx);
/*438980 point-directed knockback, angular clamps, health and player marker. */
int frame_tank_point_hit(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t point);
int frame_tank_contact_velocity(frame_data_read read,frame_data_write write,void *ctx,uint32_t object,uint32_t contact);
/*41F730 health display, retaining signed IDIV faults after its first store. */
int frame_tank_health_marker(frame_data_read read,frame_data_write write,void *ctx,uint32_t object);
#endif
